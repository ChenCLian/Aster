QT = core network testlib
TEMPLATE = app
TARGET = aster_download_tests
CONFIG += console c++17 testcase
CONFIG -= app_bundle debug_and_release debug_and_release_target
INCLUDEPATH += ../src
DESTDIR = $$OUT_PWD/../bin
include(../config/archive.pri)
SOURCES += download_tests.cpp ../src/update/release_version.cpp ../src/update/release_feed.cpp ../src/update/download_package.cpp ../src/update/package_download.cpp
HEADERS += ../src/update/package_download.h
win32-msvc:QMAKE_CXXFLAGS += /utf-8
