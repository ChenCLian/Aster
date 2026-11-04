QT = core gui widgets sql printsupport
TEMPLATE = app
include(../config/version.pri)
TARGET = Aster
CONFIG += c++17
CONFIG -= debug_and_release debug_and_release_target console
macx:CONFIG += app_bundle
else:CONFIG -= app_bundle
win32:RC_ICONS = $$PWD/../resources/app/aster.ico
macx:ICON = $$PWD/../resources/app/aster.icns
DISTFILES += ../resources/app/aster.ico ../resources/app/aster.icns
INCLUDEPATH += ../src
SOURCES += main.cpp
DESTDIR = $$OUT_PWD/../bin
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
