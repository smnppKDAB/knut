#include "predicates.h"

#include "languages.h"
#include "parser.h"
#include "tree.h"

#include "spdlog/spdlog.h"

#include <set>

#include <QRegularExpression>

namespace treesitter {

Predicates::Filters Predicates::filters()
{
    Predicates::Filters filters;
#define REGISTER_FILTER(NAME)                                                                                          \
    filters.filterFunctions[#NAME "?"] = &Predicates::filter_##NAME;                                                   \
    filters.checkFunctions[#NAME "?"] = &Predicates::checkFilter_##NAME

    REGISTER_FILTER(eq);
    REGISTER_FILTER(match);
    REGISTER_FILTER(in_message_map);
#undef REGISTER_FILTER

    return filters;
}

std::optional<QString> Predicates::checkPredicate(const Query::Predicate &predicate)
{
    const auto filters = Predicates::filters();
    auto it = filters.checkFunctions.find(predicate.name);
    if (it != filters.checkFunctions.cend()) {
        return it->second(predicate.arguments);
    }
    return "Unknown predicate";
}

Predicates::Predicates(const QString &source)
    : m_source(source)
{
}

bool Predicates::filterMatch(const QueryMatch &match) const
{
    auto patterns = match.query()->patterns();
    auto pattern = patterns.at(match.patternIndex());

    for (const auto &predicate : pattern.predicates) {
        const auto filters = Predicates::filters();
        const auto &it = filters.filterFunctions.find(predicate.name);
        if (it != filters.filterFunctions.cend()) {
            const auto &filterPredicate = it->second;
            if (!(this->*(filterPredicate))(match, predicate.arguments)) {
                return false;
            }
        }
    }

    return true;
}

std::optional<QString> Predicates::checkFilter_eq(const Predicates::PredicateArguments &arguments)
{
    if (arguments.size() < 2) {
        return "Too few arguments";
    }
    return {};
}

bool Predicates::filter_eq(const QueryMatch &match,
                           const QVector<std::variant<Query::Capture, QString>> &arguments) const
{
    std::set<QString> texts;

    auto matched = matchArguments(match, arguments);
    for (const auto &arg : matched) {
        if (const auto *capture = std::get_if<QueryMatch::Capture>(&arg)) {
            texts.emplace(capture->node.textIn(m_source));
        } else if (const auto *string = std::get_if<QString>(&arg)) {
            texts.emplace(*string);
        } else if (std::holds_alternative<MissingCapture>(arg)) {
            spdlog::warn("Predicates: #eq? - Unmatched capture!");
            // Insert an empty string into the set if we find an unmatched capture.
            // This likely means we have encountered a quantified capture that matched 0 times.
            // By inserting an empty string, we can check that all other things are also "empty".
            texts.emplace("");
        } else {
            spdlog::warn("Predicates: #eq? - Impossible argument type!");
            return false;
        }
    }
    return texts.size() == 1;
}

std::optional<QString> Predicates::checkFilter_match(const Predicates::PredicateArguments &arguments)
{
    if (arguments.size() < 2) {
        return QString("Too few arguments");
    }

    if (const auto regexString = std::get_if<QString>(&arguments.first())) {
        QRegularExpression regex(*regexString);
        if (!regex.isValid()) {
            return "Invalid Regex";
        }
    } else {
        return "Missing regex";
    }

    for (const auto &arg : arguments.last(arguments.size() - 1)) {
        if (!std::holds_alternative<Query::Capture>(arg)) {
            return "Argument is not a capture";
        }
    }

    return {};
}

bool Predicates::filter_match(const QueryMatch &match,
                              const QVector<std::variant<Query::Capture, QString>> &arguments) const
{
    auto matched = matchArguments(match, arguments);

    if (arguments.size() < 2) {
        return false;
    }

    if (const auto regexString = std::get_if<QString>(&matched.first())) {
        QRegularExpression regex(*regexString);
        if (!regex.isValid()) {
            spdlog::warn("Predicates: #match? - Invalid regex");
            return false;
        }

        for (const auto &argument : matched.last(matched.size() - 1)) {
            if (const auto *capture = std::get_if<QueryMatch::Capture>(&argument)) {
                auto source = capture->node.textIn(m_source);
                if (!regex.match(source).hasMatch()) {
                    return false;
                }
            } else if (std::holds_alternative<MissingCapture>(argument)) {
                spdlog::warn("Predicates: #match? - Unmatched capture argument");
                return false;
            } else {
                spdlog::warn("Predicates: #match? - Argument is not a capture");
                return false;
            }
        }
    } else {

        spdlog::warn("Predicates: #match? - First argument is not a string");
        return false;
    }

    return true;
}

void Predicates::insertCache(std::unique_ptr<PredicateCache> cache) const
{
    m_caches.emplace_back(std::move(cache));
}

class MessageMapCache : public PredicateCache
{
public:
    MessageMapCache(const Node &begin, const Node &end)
        : m_begin(begin)
        , m_end(end)
    {
    }

    virtual ~MessageMapCache() = default;

    Node m_begin;
    Node m_end;
};

void Predicates::findMessageMap() const
{
    if (findCache<MessageMapCache>()) {
        // Already found it!
        return;
    }

    if (!m_rootNode.has_value()) {
        spdlog::warn("Predicates::findMessageMap: No rootNode!");
        return;
    }

    auto query = std::make_shared<Query>(tree_sitter_cpp() /*in_message_map only makes sense in C++*/, R"EOF(
(
(expression_statement
    (call_expression
        function: (identifier) @begin (#eq? @begin "BEGIN_MESSAGE_MAP")
        arguments: (argument_list . (_) @class)))
.
(expression_statement)*
.
(expression_statement (call_expression
    function: (identifier) @end (#eq? @end "END_MESSAGE_MAP")))
)
    )EOF");

    QueryCursor cursor;
    cursor.execute(query, *m_rootNode, std::make_unique<Predicates>(m_source));

    auto match = cursor.nextMatch();
    if (match.has_value()) {
        auto begin = match->capturesNamed("begin");
        auto end = match->capturesNamed("end");

        if (!begin.isEmpty() && !end.isEmpty()) {
            insertCache(std::make_unique<MessageMapCache>(begin.first().node, end.first().node));
        }
    }
}

std::optional<QString> Predicates::checkFilter_in_message_map(const Predicates::PredicateArguments &arguments)
{
    if (arguments.isEmpty()) {
        return "Too few arguments";
    }

    for (const auto &arg : arguments) {
        if (!std::holds_alternative<Query::Capture>(arg)) {
            return "Non-Capture Argument";
        }
    }

    return {};
}

bool Predicates::filter_in_message_map(const QueryMatch &match, const PredicateArguments &arguments) const
{
    findMessageMap();

    if (const auto *message_map = findCache<MessageMapCache>()) {
        auto matched = matchArguments(match, arguments);

        for (const auto &argument : matched) {
            if (const auto capture = std::get_if<QueryMatch::Capture>(&argument)) {
                if (!(message_map->m_begin.endPosition() <= capture->node.startPosition()
                      && capture->node.endPosition() <= message_map->m_end.startPosition())) {
                    // We're outside of the message map
                    return false;
                }
            } else {
                spdlog::warn("Predicate: #in_message_map? - Non-Capture Argument!");
                return false;
            }
        }

        return true;
    } else {
        spdlog::warn("Predicate: #in_message_map? - No MESSAGE_MAP found!");
        return false;
    }
}

QVector<std::variant<QString, QueryMatch::Capture, Predicates::MissingCapture>>
Predicates::matchArguments(const QueryMatch &match, const Predicates::PredicateArguments &arguments) const
{
    QVector<std::variant<QString, QueryMatch::Capture, Predicates::MissingCapture>> result;

    for (const auto &argument : arguments) {
        if (const auto string = std::get_if<QString>(&argument)) {
            result.emplace_back(*string);
        } else if (const auto captureArgument = std::get_if<Query::Capture>(&argument)) {
            const auto captures = match.captures();
            bool found = false;
            // Multiple captures for the same ID may exist, if quantifiers are used.
            // Add all of them.
            for (const auto &capture : captures) {
                if (capture.id == captureArgument->id) {
                    result.emplace_back(capture);
                    found = true;
                }
            }
            if (!found) {
                result.emplace_back(MissingCapture {.capture = *captureArgument});
            }
        }
    }
    return result;
}

void Predicates::setRootNode(const Node &node)
{
    m_rootNode = node;
}

}