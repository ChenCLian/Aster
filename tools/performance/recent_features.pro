QT = core gui widgets sql printsupport network testlib
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_recent_features_benchmark
SOURCES += recent_features_benchmark.cpp
INCLUDEPATH += $$PWD/../../src
isEmpty(ASTER_BUILD_ROOT):error(Supply ASTER_BUILD_ROOT pointing to a complete Aster build)
LIBS += -L"$$ASTER_BUILD_ROOT/lib" -laster_desktop_ui -laster_application -laster_science -laster_io -laster_core
include(../../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
unix:LIBS += -pthread
