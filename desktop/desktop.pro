QT = core gui widgets sql printsupport
TEMPLATE = app
include(../config/version.pri)
TARGET = AstroBatch
win32:QMAKE_TARGET_PRODUCT = AstroBatch
win32:QMAKE_TARGET_DESCRIPTION = AstroBatch
CONFIG += c++17
CONFIG -= debug_and_release debug_and_release_target console
macx:CONFIG += app_bundle
else:CONFIG -= app_bundle
win32:RC_ICONS = $$PWD/../resources/app/AstroBatch.ico
macx:ICON = $$PWD/../resources/app/AstroBatch.icns
DISTFILES += ../resources/app/AstroBatch.ico ../resources/app/AstroBatch.icns
INCLUDEPATH += ../src
SOURCES += main.cpp
DESTDIR = $$OUT_PWD/../bin
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
