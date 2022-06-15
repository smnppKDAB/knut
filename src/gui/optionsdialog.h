#pragma once

#include <QDialog>

#include <memory>

namespace Gui {

namespace Ui {
    class OptionsDialog;
}

class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget *parent = nullptr);
    ~OptionsDialog();

    void addSettings(QWidget *widget);

private:
    void openUserSettings();
    void openProjectSettings();

    void addScriptPath();
    void removeScriptPath();
    void updateScriptPaths();

    void addJsonPath();
    void removeJsonPath();
    void updateJsonPaths();

    void addReturnValue();
    void removeReturnValue();
    void changeToggleSectionSetting();

    void changePage();

private:
    std::unique_ptr<Ui::OptionsDialog> ui;
};

} // namespace Gui
