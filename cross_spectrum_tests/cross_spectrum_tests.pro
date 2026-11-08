QT = core sql testlib
TEMPLATE = app
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_cross_spectrum_tests
DESTDIR = $$OUT_PWD/../bin
INCLUDEPATH += $$PWD/../src
ASTER_MODULES = aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
unix:LIBS += -pthread
SOURCES += cross_spectrum_tests.cpp ../src/ui/results/preview_data.cpp
DISTFILES += fixtures/generate_reference.py fixtures/scipy_reference.json

DISTFILES += fixtures/real_reference.json fixtures/generate_real_reference.py fixtures/README.md
