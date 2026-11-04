QT = core gui widgets sql printsupport network testlib
TEMPLATE = app
TARGET = aster_update_tests
CONFIG += console c++17 testcase
CONFIG -= app_bundle debug_and_release debug_and_release_target
include(../config/version.pri)
INCLUDEPATH += ../src
SOURCES += update_tests.cpp
DESTDIR = $$OUT_PWD/../bin
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
