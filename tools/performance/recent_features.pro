QT = core gui widgets sql printsupport network testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_recent_features_benchmark
SOURCES += recent_features_benchmark.cpp
INCLUDEPATH += $$PWD/../../src
isEmpty(ASTER_BUILD_ROOT):error(Supply ASTER_BUILD_ROOT pointing to a complete Aster build)
ASTER_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
include(../../config/link_modules.pri)
include(../../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
unix:LIBS += -pthread
