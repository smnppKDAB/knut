#ifndef CONVERTER
#define CONVERTER

#include <QJsonObject>

QJsonObject convertDialog(const QJsonObject &dialog);
QJsonObject convertDialogs(const QJsonObject &rootObject);

#endif // CONVERTER

