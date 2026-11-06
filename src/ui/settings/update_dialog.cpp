// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/common/choice_editor.h"
#include "ui/settings/update_dialog.h"
#include "ui/common/presentation.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QFormLayout>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace aster::ui {
UpdateDialog::UpdateDialog(QWidget *parent) : QDialog(parent) {
    setObjectName("updateDialog");
    setWindowTitle(text("update.title"));
    resize(620, 440);
    setMinimumSize(520, 360);
    QFile file(":/updates/github.json");
    file.open(QIODevice::ReadOnly);
    const auto config = updates::UpdateConfig::fromJson(file.readAll());
    auto *checker = new updates::UpdateChecker(config, this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(":/app/aster.png").pixmap(48, 48));
    header->addWidget(icon);
    auto *identity = new QVBoxLayout;
    auto *title = new QLabel("Aster");
    title->setObjectName("PageTitle");
    identity->addWidget(title);
    identity->addWidget(new QLabel(text("update.installed") + "  " + QCoreApplication::applicationVersion()));
    header->addLayout(identity);
    header->addStretch();
    layout->addLayout(header);

    auto *sourcePanel = new QFrame;
    sourcePanel->setObjectName("Panel");
    auto *form = new QFormLayout(sourcePanel);
    form->setContentsMargins(12, 10, 12, 10);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    auto *repo = new QLabel(config.releasesUrl().toString());
    repo->setTextFormat(Qt::PlainText);
    repo->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    repo->setWordWrap(true);
    form->addRow(text("update.repository"), repo);
    auto *channel = new ChoiceEditor;
    channel->setObjectName("updateChannel");
    channel->addItem(text("update.stable"), false);
    channel->addItem(text("update.preview"), true);
    form->addRow(text("update.channel"), channel);
    layout->addWidget(sourcePanel);

    auto *row = new QHBoxLayout;
    auto *check = new QPushButton(text("update.check"));
    check->setObjectName("updateCheck");
    auto *cancel = new QPushButton(text("update.cancel"));
    cancel->setObjectName("updateCancel");
    cancel->setEnabled(false);
    auto *status = new QLabel;
    status->setObjectName("updateStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    row->addWidget(check);
    row->addWidget(cancel);
    row->addWidget(status, 1);
    layout->addLayout(row);
    auto *progress = new QProgressBar;
    progress->setRange(0, 0);
    progress->setMaximumHeight(5);
    progress->setTextVisible(false);
    progress->hide();
    layout->addWidget(progress);

    auto *notesTitle = new QLabel(text("update.notes"));
    notesTitle->setObjectName("SectionTitle");
    layout->addWidget(notesTitle);
    auto *notes = new QPlainTextEdit;
    notes->setObjectName("updateNotes");
    notes->setReadOnly(true);
    layout->addWidget(notes, 1);

    auto *footer = new QHBoxLayout;
    auto *releases = new QPushButton(text("update.releases"));
    releases->setObjectName("updateReleases");
    footer->addWidget(releases);
    footer->addStretch();
    auto *open = new QPushButton(text("update.open"));
    open->setObjectName("updateOpen");
    open->setEnabled(false);
    footer->addWidget(open);
    auto *close = new QPushButton(text("update.close"));
    footer->addWidget(close);
    layout->addLayout(footer);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(this, &QDialog::finished, checker, &updates::UpdateChecker::cancel);
    connect(cancel, &QPushButton::clicked, checker, &updates::UpdateChecker::cancel);
    connect(check, &QPushButton::clicked, this, [=] {
        check->setEnabled(false);
        cancel->setEnabled(true);
        channel->setEnabled(false);
        open->setEnabled(false);
        open->setProperty("releaseUrl", QVariant());
        notes->clear();
        status->setText(text("update.checking"));
        progress->show();
        checker->check(QCoreApplication::applicationVersion(), channel->currentData().toBool());
    });
    connect(checker, &updates::UpdateChecker::finished, this, [=](updates::UpdateResult result) {
        check->setEnabled(true);
        cancel->setEnabled(false);
        channel->setEnabled(true);
        progress->hide();
        auto message = text(qPrintable(result.messageKey));
        const bool available = result.state == updates::UpdateResult::State::Available && result.release.has_value();
        if (available) message = message.arg(result.release->tag);
        status->setText(message);
        if (result.release) {
            const auto &release = *result.release;
            const auto heading = release.name.isEmpty() ? release.tag : release.name;
            notes->setPlainText(heading + "\n\n" + (release.notes.trimmed().isEmpty() ? text("update.notes_empty") : release.notes));
            open->setProperty("releaseUrl", release.pageUrl);
        } else {
            open->setProperty("releaseUrl", QVariant());
        }
        open->setEnabled(available);
    });
    const auto browse = [status](const QUrl &url) {
        if (!url.isValid() || url.isEmpty() || !QDesktopServices::openUrl(url))
            status->setText(text("update.error_browser"));
    };
    connect(open, &QPushButton::clicked, this, [=] { browse(open->property("releaseUrl").toUrl()); });
    connect(releases, &QPushButton::clicked, this, [=] { browse(config.releasesUrl()); });
}
} // namespace aster::ui
