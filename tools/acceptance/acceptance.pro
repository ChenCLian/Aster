QT = core gui widgets sql printsupport network testlib
TEMPLATE = app
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_interaction_acceptance
SOURCES += interaction_acceptance.cpp
INCLUDEPATH += $$PWD/../../src
isEmpty(ASTER_BUILD_ROOT):error(Supply ASTER_BUILD_ROOT pointing to a complete Aster build)
LIBS += -L"$$ASTER_BUILD_ROOT/lib" -laster_desktop_ui -laster_application -laster_science -laster_io -laster_core
ASTER_ACCEPTANCE_MODULES = aster_desktop_ui aster_application aster_science aster_io aster_core
for(module, ASTER_ACCEPTANCE_MODULES) {
    win32-msvc:PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/$${module}.lib
    else:PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/lib$${module}.a
}
include(../../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
unix:LIBS += -pthread
