QT = core gui widgets sql testlib printsupport
TEMPLATE = app
CONFIG += testcase console c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_workspace_tests
DESTDIR = $$OUT_PWD/../bin
INCLUDEPATH += ../src
SOURCES += workspace_tests.cpp
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8

HEADERS += ../tests/support/workspace_fixture.h
