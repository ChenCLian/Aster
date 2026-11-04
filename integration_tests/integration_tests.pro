QT = core gui widgets sql testlib printsupport
TEMPLATE = app
CONFIG += testcase console c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_integration_tests
DESTDIR = $$OUT_PWD/../bin
INCLUDEPATH += ../src
SOURCES += main.cpp workspace_services_tests.cpp interaction_safety_tests.cpp
HEADERS += integration_suites.h
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += record_directory_tests.cpp

SOURCES += database_transfer_tests.cpp
