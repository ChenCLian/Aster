QT = core
TEMPLATE = lib
CONFIG += staticlib c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_core
DESTDIR = $$OUT_PWD/../lib
INCLUDEPATH += $$PWD/../src
win32-msvc:QMAKE_CXXFLAGS += /utf-8

SOURCES += \
    ../src/core/error_info.cpp \
    ../src/model/data_types.cpp \
    ../src/model/workflow_types.cpp \
    ../src/model/workflow_codec.cpp \
    ../src/core/task.cpp \
    ../src/core/task_registry.cpp \
    ../src/core/workflow_model.cpp \
    ../src/core/workflow_executor.cpp \
    ../src/processing/builtin_tasks.cpp \
    ../src/processing/input_series_task.cpp \
    ../src/processing/sine_source_task.cpp \
    ../src/processing/linear_detrend_task.cpp \
    ../src/processing/affine_task.cpp \
    ../src/processing/series_summary_task.cpp

HEADERS += \
    ../src/core/error_info.h \
    ../src/model/data_types.h \
    ../src/model/workflow_types.h \
    ../src/model/workflow_codec.h \
    ../src/core/task.h \
    ../src/core/task_registry.h \
    ../src/core/workflow_model.h \
    ../src/core/workflow_executor.h \
    ../src/processing/process_definitions.h \
    ../src/processing/builtin_tasks.h \
    ../src/processing/task_helpers.h \
    ../src/processing/input_series_task.h \
    ../src/processing/sine_source_task.h \
    ../src/processing/linear_detrend_task.h \
    ../src/processing/affine_task.h \
    ../src/processing/series_summary_task.h

HEADERS += ../src/processing/task_categories.h
