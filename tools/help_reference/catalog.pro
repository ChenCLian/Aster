QT = core sql
TEMPLATE = app
CONFIG += console c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_help_catalog
isEmpty(ASTER_BUILD_ROOT) {
    contains(CONFIG, help_reference):ASTER_BUILD_ROOT = $$clean_path($$OUT_PWD/../..)
    else:error(Supply ASTER_BUILD_ROOT pointing to a root Aster build)
}
DESTDIR = $$ASTER_BUILD_ROOT/bin
INCLUDEPATH += $$PWD/../../src
SOURCES += main.cpp
include(../../config/version.pri)
LIBS += -L"$$ASTER_BUILD_ROOT/lib" -laster_science -laster_io -laster_core
ASTER_HELP_MODULES = aster_science aster_io aster_core
for(module, ASTER_HELP_MODULES) {
    win32-msvc:PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/$${module}.lib
    else:PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/lib$${module}.a
}
include(../../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
