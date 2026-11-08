// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
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
#include <QDesktopServices>
#include <QUrl>
#include <QFrame>
#include "ui/help/help_dialog.h"
namespace aster::ui
{
SettingsDialog::SettingsDialog(AppSettings settings, QWidget *parent)
    : QDialog(parent), m_values(std::move(settings))
{
    setWindowTitle(text("ui.settings"));
    setObjectName("settingsDialog");
    setMinimumWidth(440);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto *form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(8);

    auto *path = new QLineEdit(m_values.databasePath);
    path->setObjectName("settingsDatabase");
    path->setMinimumWidth(0);
    path->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    path->setToolTip(path->text());
    path->setCursorPosition(0);
    connect(path, &QLineEdit::textChanged, path, &QWidget::setToolTip);
    auto *pathRow = new QWidget;
    auto *pl = new QHBoxLayout(pathRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(path, 1);
    auto *browse = new QPushButton(text("ui.browse"));
    browse->setAutoDefault(false);
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
        openHelp(databaseHelp->window(), userGuideTopic("database"));
    });
    layout->addWidget(databaseLabel);
    layout->addWidget(pathRow);
    layout->addSpacing(4);
    layout->addLayout(form);
    connect(browse, &QPushButton::clicked, this,
            [this, path]
            {
                auto value = QFileDialog::getSaveFileName(this, text("ui.choose_database"),
                                                          path->text(), "SQLite (*.sqlite *.db)");
                if (!value.isEmpty())
                    path->setText(value);
            });
    auto *language = new ChoiceEditor;
    language->setObjectName("settingsLanguage");
    language->setToolTip(text("ui.language_restart_hint"));
    language->addItem("简体中文", "zh");
    language->addItem("English", "en");
    language->setCurrentIndex(language->findData(m_values.language));
    form->addRow(text("ui.interface_language"), language);
    auto *theme = new ChoiceEditor;
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
    auto *plotTarget = new ChoiceEditor;
    plotTarget->setObjectName("settingsDefaultPlotTarget");
    plotTarget->addItem(text("ui.default_plot_new_page"), true);
    plotTarget->addItem(text("ui.default_plot_current_page"), false);
    plotTarget->setCurrentIndex(plotTarget->findData(m_values.defaultPlotNewPage));
    plotTarget->setToolTip(text("ui.default_plot_target_help"));
    form->addRow(text("ui.default_plot_target"), plotTarget);
    auto *divider = new QFrame;
    divider->setObjectName("settingsDivider");
    divider->setFixedHeight(1);
    layout->addWidget(divider);
    auto *supportRow = new QHBoxLayout;
    supportRow->setSpacing(8);
    const QKeySequence helpKey(Qt::Key_F1);
    auto *manual = new QPushButton(shortcutLabel(text("help.title"), helpKey));
    manual->setObjectName("openUserGuide");
    manual->setToolTip(manual->text());
    manual->setShortcut(helpKey);
    supportRow->addWidget(manual, 1);
    connect(manual, &QPushButton::clicked, this, [this] { openHelp(this, userGuideTopic()); });
    auto *contact = new QPushButton(text("ui.contact_us"));
    contact->setObjectName("settingsContact");
    contact->setToolTip("GitHub");
    supportRow->addWidget(contact, 1);
    connect(contact, &QPushButton::clicked, this, [this] {
        if (!QDesktopServices::openUrl(QUrl("https://github.com/ChenCLian/Aster/blob/main/AUTHORS.md")))
            QMessageBox::warning(this, text("ui.settings"), text("ui.open_link_failed"));
    });
    auto *updates = new QPushButton(text("update.check"));
    updates->setObjectName("settingsCheckUpdates");
    supportRow->addWidget(updates, 1);
    layout->addLayout(supportRow);
    connect(updates, &QPushButton::clicked, this, [this] { UpdateDialog dialog(this); dialog.exec(); });
    auto *footer = new QHBoxLayout;
    auto *version = new QLabel(text("update.installed") + "  " + QCoreApplication::applicationVersion());
    version->setObjectName("Muted");
    footer->addWidget(version);
    auto *license = new QPushButton(text("ui.license"));
    license->setObjectName("settingsLicense");
    license->setAutoDefault(false);
    footer->addWidget(license);
    connect(license, &QPushButton::clicked, this, [this] { openHelp(this, licenseTopic()); });
    footer->addStretch();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    footer->addWidget(buttons);
    layout->addLayout(footer);
    for (auto *button : {manual, contact, updates}) button->setAutoDefault(false);
    resize(480, sizeHint().height());
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto *databaseCheck = new RecordDirectoryService(this);
    connect(this, &QDialog::finished, databaseCheck, &RecordDirectoryService::invalidate);
    connect(databaseCheck, &RecordDirectoryService::listed, this,
            [this, buttons, path, browse, language, theme, threads, plotTarget](RecordList records, QStringList, int)
            {
                buttons->setEnabled(true);
                path->setEnabled(true);
                browse->setEnabled(true);
                language->setEnabled(true);
                theme->setEnabled(true);
                threads->setEnabled(true);
                plotTarget->setEnabled(true);
                if (!records.error.isEmpty())
                {
                    QMessageBox::warning(this, text("ui.database_unavailable"), records.error);
                    return;
                }
                m_values.databasePath = QFileInfo(path->text().trimmed()).absoluteFilePath();
                accept();
            });
    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, buttons, path, browse, databaseCheck, language, theme, threads, plotTarget]
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
                m_values.defaultPlotNewPage = plotTarget->currentData().toBool();
                language->setEnabled(false);
                theme->setEnabled(false);
                threads->setEnabled(false);
                plotTarget->setEnabled(false);
                databaseCheck->query(path->text().trimmed(), {}, 0);
            });
}
} // namespace aster::ui
