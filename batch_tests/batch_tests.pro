QT = core testlib
TEMPLATE = app
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_batch_tests
DESTDIR = $$OUT_PWD/../bin
INCLUDEPATH += $$PWD/../src
ASTER_MODULES = aster_core
include(../config/link_modules.pri)
unix:LIBS += -pthread
win32-msvc:QMAKE_CXXFLAGS += /utf-8
SOURCES += batch_tests.cpp
