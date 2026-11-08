QT = core sql
TEMPLATE = lib
SOURCES += ../src/processing/orbital_table_tasks.cpp
HEADERS += ../src/processing/orbital_table_tasks.h
CONFIG += staticlib c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_science
DESTDIR = $$OUT_PWD/../lib
INCLUDEPATH += $$PWD/../src $$PWD/../src/third_party/wavelib
win32-msvc:QMAKE_CXXFLAGS += /utf-8
include(../config/scientific_dependencies.pri)
include(numeric_backend_sources.pri)
SOURCES += ../src/processing/scientific_registry.cpp ../src/processing/science_helpers.cpp \
    ../src/processing/file_source_task.cpp ../src/processing/preprocess_task.cpp \
    ../src/processing/lowess_task.cpp ../src/processing/mtm_task.cpp ../src/processing/wavelet_task.cpp \
    ../src/math/numeric_utils.cpp ../src/math/spectrum/mtm_spectrum.cpp \
    ../src/math/spectrum/spectrum_peaks.cpp ../src/math/statistics/significance.cpp \
    ../src/math/statistics/spectral_statistics.cpp ../src/math/statistics/swa_background.cpp
HEADERS += ../src/processing/scientific_registry.h ../src/processing/science_helpers.h \
    ../src/processing/file_source_task.h ../src/processing/preprocess_task.h \
    ../src/processing/lowess_task.h ../src/processing/mtm_task.h ../src/processing/wavelet_task.h \
    ../src/math/numeric_utils.h ../src/math/spectrum/mtm_spectrum.h \
    ../src/math/spectrum/spectrum_peaks.h ../src/math/statistics/significance.h \
    ../src/third_party/lowess/Lowess.h
WAVELIB_DIR = $$PWD/../src/third_party/wavelib
SOURCES += $$WAVELIB_DIR/conv.c $$WAVELIB_DIR/cwt.c $$WAVELIB_DIR/cwtmath.c \
    $$WAVELIB_DIR/hsfft.c $$WAVELIB_DIR/real.c $$WAVELIB_DIR/wavefilt.c \
    $$WAVELIB_DIR/wavefunc.c $$WAVELIB_DIR/wavelib.c $$WAVELIB_DIR/wtmath.c
HEADERS += $$files($$WAVELIB_DIR/*.h)
DISTFILES += ../src/third_party/wavelib/COPYRIGHT

HEADERS += ../src/processing/database_source_task.h

SOURCES += ../src/math/wavelet/cwt_support.cpp
HEADERS += ../src/math/wavelet/cwt_support.h

SOURCES += ../src/math/statistics/mtm_simulation.cpp ../src/processing/mtm_simulation_task.cpp
SOURCES += ../src/math/statistics/monte_carlo_test.cpp
HEADERS += ../src/math/statistics/monte_carlo_test.h
HEADERS += ../src/math/statistics/mtm_simulation.h ../src/processing/mtm_simulation_task.h

SOURCES += ../src/math/statistics/ar1_nuisance.cpp
HEADERS += ../src/math/statistics/ar1_nuisance.h
SOURCES += ../src/math/statistics/ar1_global.cpp ../src/processing/ar1_global_task.cpp \
    ../src/processing/services/ar1_reference_cache.cpp
HEADERS += ../src/math/statistics/ar1_global.h ../src/processing/ar1_global_task.h \
    ../src/processing/services/ar1_reference_cache.h

SOURCES += ../src/math/astronomy/astronomical_curves.cpp ../src/processing/astronomy_source_tasks.cpp
HEADERS += ../src/math/astronomy/astronomical_curves.h ../src/processing/astronomy_source_tasks.h

SOURCES += ../src/math/astronomy/seasonal_insolation.cpp ../src/processing/clean_series_task.cpp ../src/processing/services/series_cleanup.cpp
HEADERS += ../src/math/astronomy/seasonal_insolation.h ../src/processing/clean_series_task.h ../src/processing/services/series_cleanup.h

SOURCES += ../src/math/filtering/bandpass.cpp ../src/processing/filter_tasks.cpp ../src/processing/insolation_map_task.cpp
HEADERS += ../src/math/filtering/bandpass.h ../src/processing/filter_tasks.h ../src/processing/insolation_map_task.h

SOURCES += ../src/processing/evolutionary_tasks.cpp ../src/processing/series_utility_tasks.cpp \
    ../src/math/spectrum/evolutionary_spectrum.cpp ../src/math/series/series_operations.cpp
HEADERS += ../src/processing/evolutionary_tasks.h ../src/processing/series_utility_tasks.h \
    ../src/processing/task_categories.h ../src/math/spectrum/evolutionary_spectrum.h \
    ../src/math/series/series_operations.h

SOURCES += ../src/math/smoothing/smoothing.cpp ../src/processing/basic_signal_tasks.cpp
HEADERS += ../src/math/smoothing/smoothing.h ../src/processing/basic_signal_tasks.h

SOURCES += ../src/math/spectrum/lomb_scargle.cpp ../src/math/filtering/butterworth.cpp ../src/processing/spectral_extension_tasks.cpp
HEADERS += ../src/math/spectrum/lomb_scargle.h ../src/math/filtering/butterworth.h ../src/processing/spectral_extension_tasks.h

SOURCES += ../src/math/chronology/age_model.cpp ../src/processing/chronology_tasks.cpp
HEADERS += ../src/math/chronology/age_model.h ../src/processing/chronology_tasks.h

SOURCES += ../src/math/astronomy/milankovitch_periods.cpp ../src/math/chronology/cycle_age.cpp ../src/processing/cycle_tuning_tasks.cpp
HEADERS += ../src/math/astronomy/milankovitch_periods.h ../src/math/chronology/cycle_age.h ../src/processing/cycle_tuning_tasks.h ../src/processing/chronology_helpers.h

SOURCES += ../src/math/chronology/coco_scan.cpp ../src/processing/coco_task.cpp
HEADERS += ../src/math/chronology/coco_scan.h ../src/processing/coco_task.h

SOURCES += ../src/math/chronology/coco_analysis.cpp ../src/processing/coco_analysis_tasks.cpp
HEADERS += ../src/math/chronology/coco_analysis.h ../src/math/chronology/coco_detail.h ../src/processing/coco_analysis_tasks.h ../src/processing/coco_task_support.h

SOURCES += ../src/processing/example_source_task.cpp
HEADERS += ../src/processing/example_source_task.h

SOURCES += ../src/math/synthetic/orbital_proxy.cpp ../src/processing/synthetic_orbital_task.cpp
HEADERS += ../src/math/synthetic/orbital_proxy.h ../src/processing/synthetic_orbital_task.h
RESOURCES += ../resources/synthetic/synthetic.qrc
DISTFILES += ../resources/synthetic/NOTICE.md ../resources/synthetic/SOURCE_PANGAEA_817206.txt

SOURCES += ../src/math/spectrum/cross_spectrum.cpp ../src/processing/cross_spectrum_task.cpp
HEADERS += ../src/math/spectrum/cross_spectrum.h ../src/processing/cross_spectrum_task.h
