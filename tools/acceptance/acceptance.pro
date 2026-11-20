QT = core gui widgets sql printsupport network testlib
TEMPLATE = app
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_interaction_acceptance
SOURCES += interaction_acceptance.cpp
INCLUDEPATH += $$PWD/../../src
contains(CONFIG, interaction_acceptance) {
    isEmpty(ASTER_BUILD_ROOT):ASTER_BUILD_ROOT = $$clean_path($$OUT_PWD/../..)
    DESTDIR = $$ASTER_BUILD_ROOT/bin
}
isEmpty(ASTER_BUILD_ROOT):error(Supply ASTER_BUILD_ROOT pointing to a complete AstroBatch build)
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../../config/link_modules.pri)
include(../../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
unix:LIBS += -pthread
