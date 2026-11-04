#include "ui/common/numeric_editor.h"
#include "ui/settings/settings_dialog.h"
#include "ui/settings/update_dialog.h"
#include "ui/common/presentation.h"
#include "ui/services/record_directory_service.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QCoreApplication>
#include <QToolButton>
#include <QToolTip>
namespace aster::ui
{
SettingsDialog::SettingsDialog(AppSettings settings, QWidget *parent)
    : QDialog(parent), m_values(std::move(settings))
{
    setWindowTitle(text("ui.settings"));
    setObjectName("settingsDialog");
    resize(560, 380);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setVerticalSpacing(8);
    layout->addLayout(form);
    auto *path = new QLineEdit(m_values.databasePath);
    path->setObjectName("settingsDatabase");
    auto *pathRow = new QWidget;
    auto *pl = new QHBoxLayout(pathRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(path, 1);
    auto *browse = new QPushButton(text("ui.browse"));
    pl->addWidget(browse);
    auto *databaseLabel = new QWidget;
    auto *databaseHeading = new QHBoxLayout(databaseLabel);
    databaseHeading->setContentsMargins(0, 0, 0, 0);
    auto *databaseTitle = new QLabel(text("ui.default_database_file_location"));
    databaseTitle->setBuddy(path);
    databaseHeading->addWidget(databaseTitle);
    auto *databaseHelp = new QToolButton;
    databaseHelp->setObjectName("ParameterHelp");
    databaseHelp->setText("?");
    databaseHelp->setAutoRaise(true);
    databaseHelp->setToolTip(text("ui.switching_databases_does_not_copy_or_delete_existing_records"));
    databaseHelp->setAccessibleName(databaseTitle->text());
    databaseHelp->setAccessibleDescription(databaseHelp->toolTip());
    databaseHeading->addWidget(databaseHelp);
    databaseHeading->addStretch();
    connect(databaseHelp, &QToolButton::clicked, databaseHelp, [databaseHelp] {
        QToolTip::showText(databaseHelp->mapToGlobal(QPoint(0, databaseHelp->height())),
                          databaseHelp->toolTip(), databaseHelp);
    });
    form->addRow(databaseLabel);
    form->addRow(pathRow);
    connect(browse, &QPushButton::clicked, this,
            [this, path]
            {
                auto value = QFileDialog::getSaveFileName(this, text("ui.choose_database"),
                                                          path->text(), "SQLite (*.sqlite *.db)");
                if (!value.isEmpty())
                    path->setText(value);
            });
    auto *language = new QComboBox;
    language->setObjectName("settingsLanguage");
    language->setToolTip(text("ui.language_restart_hint"));
    language->addItem("简体中文", "zh");
    language->addItem("English", "en");
    language->setCurrentIndex(language->findData(m_values.language));
    form->addRow(text("ui.interface_language"), language);
    auto *theme = new QComboBox;
    theme->setObjectName("settingsTheme");
    theme->addItem(text("ui.light"), "light");
    theme->addItem(text("ui.dark"), "dark");
    theme->setCurrentIndex(theme->findData(m_values.theme));
    form->addRow(text("ui.theme"), theme);
    auto *threads = new IntegerEditor;
    threads->setObjectName("settingsConcurrency");
    threads->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    threads->setRange(minimumConcurrency, maximumConcurrency);
    threads->setToolTip(text("ui.concurrent_input_help"));
    threads->setValue(m_values.concurrency);
    form->addRow(text("ui.concurrent_input_runs"), threads);
    auto *updateRow = new QHBoxLayout;
    updateRow->addWidget(new QLabel(text("update.installed") + "  " + QCoreApplication::applicationVersion()));
    updateRow->addStretch();
    auto *updates = new QPushButton(text("update.check"));
    updates->setObjectName("settingsCheckUpdates");
    updateRow->addWidget(updates);
    layout->addLayout(updateRow);
    connect(updates, &QPushButton::clicked, this, [this] { UpdateDialog dialog(this); dialog.exec(); });
    layout->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *databaseCheck = new RecordDirectoryService(this);
    connect(this, &QDialog::finished, databaseCheck, &RecordDirectoryService::invalidate);
    connect(databaseCheck, &RecordDirectoryService::listed, this,
            [this, buttons, path, browse, language, theme, threads](RecordList records, QStringList, int)
            {
                buttons->setEnabled(true);
                path->setEnabled(true);
                browse->setEnabled(true);
                language->setEnabled(true);
                theme->setEnabled(true);
                threads->setEnabled(true);
                if (!records.error.isEmpty())
                {
                    QMessageBox::warning(this, text("ui.database_unavailable"), records.error);
                    return;
                }
                m_values.databasePath = QFileInfo(path->text().trimmed()).absoluteFilePath();
                accept();
            });
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, buttons, path, browse, databaseCheck, language, theme, threads]
            {
                buttons->setEnabled(false);
                path->setEnabled(false);
                browse->setEnabled(false);
                if (path->text().trimmed().isEmpty())
                {
                    buttons->setEnabled(true);
                    path->setEnabled(true);
                    browse->setEnabled(true);
                    QMessageBox::warning(this, text("ui.database_unavailable"),
                                         text("ui.choose_a_database_file_location"));
                    return;
                }
                m_values.language = language->currentData().toString();
                m_values.theme = theme->currentData().toString();
                m_values.concurrency = threads->value();
                language->setEnabled(false);
                theme->setEnabled(false);
                threads->setEnabled(false);
                databaseCheck->query(path->text().trimmed(), {}, 0);
            });
}
} // namespace aster::ui
