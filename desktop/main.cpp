// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/app/main_window.h"
#include "ui/common/presentation.h"
#include "ui/settings/theme_manager.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QTextStream>
#include <QProcess>
#include <QDir>
#include <QMessageBox>
#include "math/backend/numeric_backend.h"
using namespace aster::ui;
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Aster");
    QCoreApplication::setApplicationName("Aster_Workspace");
    QCoreApplication::setApplicationVersion(ASTER_VERSION);
    QCommandLineParser parser;
    parser.setApplicationDescription("Aster desktop workflow workspace");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"numeric-backend", "Print the actual numerical backend and exit"});
    parser.addOption({"database", "Use a SQLite result database", "path"});
    parser.addOption({"language", "Override UI language: zh or en", "language"});
    parser.addOption({"theme", "Override theme: light or dark", "theme"});
    parser.process(app);
    auto settings = AppSettings::load();
    if (parser.isSet("numeric-backend")) {
        try {
            const auto info = aster::numeric::backend::backendInfo();
            QTextStream(stdout) << QString::fromStdString(info.name) << '\n'
                << QString::fromStdString(info.linearAlgebra) << '\n'
                << QString::fromStdString(info.fft) << '\n'
                << "Threads per call: " << info.threadsPerCall << '\n'
                << "Serialized LAPACK calls: " << info.serializesLapackCalls << '\n';
            return 0;
        } catch (const std::exception& error) {
            QTextStream(stderr) << error.what() << '\n';
            return 1;
        }
    }
    if (parser.isSet("database")) settings.databasePath = parser.value("database");
    if (parser.isSet("language")) settings.language = parser.value("language");
    if (parser.isSet("theme")) settings.theme = parser.value("theme");
    setLanguage(settings.language);
    ThemeManager::instance().apply(app, settings.theme);
    // The theme manager initializes the resource collection in desktop_ui.
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app/aster.png")));
    int exitCode = 0;
    bool restart = false;
    {
        MainWindow window(settings, !parser.isSet("database"));
        window.show();
        exitCode = app.exec();
        restart = window.restartRequested();
        settings = window.settings();
    } // Release window-owned workers and database connections before relaunching.
    if (restart)
    {
        // Rebuild current overrides, rather than replaying a stale --language.
        QStringList arguments{"--language", settings.language, "--theme", settings.theme};
        if (parser.isSet("database"))
            arguments << "--database" << settings.databasePath;
        QProcess process;
        process.setProgram(QCoreApplication::applicationFilePath());
        process.setArguments(arguments);
        process.setWorkingDirectory(QDir::currentPath());
        if (!process.startDetached())
        {
            QMessageBox::critical(nullptr, text("ui.restart_aster"),
                text("ui.restart_failed").arg(process.errorString()));
            return 1;
        }
    }
    return exitCode;
}
