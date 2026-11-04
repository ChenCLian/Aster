QT += core sql testlib
QT -= gui
TEMPLATE = app
TARGET = aster_analysis_tests
CONFIG += console c++17 testcase
CONFIG -= app_bundle debug_and_release debug_and_release_target
INCLUDEPATH += ../src
SOURCES += analysis_tests.cpp spectral_audit.cpp
HEADERS += spectral_audit.h
DESTDIR = $$OUT_PWD/../bin
ASTER_MODULES = aster_application aster_science aster_io aster_core
include(../config/link_modules.pri)
include(../config/scientific_dependencies.pri)
win32-msvc:QMAKE_CXXFLAGS += /utf-8
DISTFILES += fixtures/mtm_scipy.json fixtures/generate_mtm_reference.py
DISTFILES += fixtures/foundation_reference.json fixtures/generate_foundation_reference.py
DISTFILES += fixtures/lowess_statsmodels_reference.json fixtures/generate_lowess_statsmodels_reference.py

DISTFILES += fixtures/spectral_reference.json fixtures/generate_spectral_reference.py

SOURCES += wavelet_audit.cpp
HEADERS += wavelet_audit.h
DISTFILES += fixtures/wavelet_reference.json fixtures/generate_wavelet_reference.py
SOURCES += transfer_audit.cpp
HEADERS += transfer_audit.h

SOURCES += mtm_simulation_audit.cpp
HEADERS += mtm_simulation_audit.h
DISTFILES += fixtures/mtm_simulation_reference.json fixtures/generate_mtm_simulation_reference.py
SOURCES += acycle_alignment_audit.cpp
HEADERS += acycle_alignment_audit.h
SOURCES += backend_audit.cpp
HEADERS += backend_audit.h
DISTFILES += fixtures/acycle_reference.json fixtures/generate_acycle_reference.py
SOURCES += ar1_global_audit.cpp
HEADERS += ar1_global_audit.h
DISTFILES += native_cwt_allocations.c

SOURCES += astronomy_audit.cpp
HEADERS += astronomy_audit.h
DISTFILES += fixtures/astronomy_reference.json fixtures/generate_astronomy_reference.py fixtures/acycle_etp_la2004.csv

SOURCES += seasonal_audit.cpp
HEADERS += seasonal_audit.h
DISTFILES += fixtures/seasonal_reference.json fixtures/generate_seasonal_reference.py

SOURCES += filter_audit.cpp
HEADERS += filter_audit.h
DISTFILES += fixtures/filter_reference.json fixtures/generate_filter_reference.py
SOURCES += orbital_tables_audit.cpp
HEADERS += orbital_tables_audit.h
DISTFILES += fixtures/orbital_tables_reference.json
DISTFILES += fixtures/generate_orbital_reference.py

SOURCES += evolution_audit.cpp
HEADERS += evolution_audit.h
DISTFILES += fixtures/evolution_reference.json fixtures/generate_evolution_reference.py

SOURCES += basic_signal_audit.cpp
HEADERS += basic_signal_audit.h
DISTFILES += fixtures/basic_signal_reference.json fixtures/generate_basic_signal_reference.py

SOURCES += spectral_extension_audit.cpp
HEADERS += spectral_extension_audit.h
DISTFILES += fixtures/spectral_extension_reference.json fixtures/generate_spectral_extension_reference.py

SOURCES += chronology_audit.cpp
HEADERS += chronology_audit.h
DISTFILES += fixtures/chronology_reference.json fixtures/generate_chronology_reference.py

SOURCES += cycle_tuning_audit.cpp
HEADERS += cycle_tuning_audit.h
DISTFILES += fixtures/cycle_tuning_reference.json fixtures/generate_cycle_tuning_reference.py

SOURCES += coco_audit.cpp
HEADERS += coco_audit.h
DISTFILES += fixtures/coco_reference.json fixtures/generate_coco_reference.py
DISTFILES += fixtures/coco_acceptance.csv

SOURCES += coco_analysis_audit.cpp
HEADERS += coco_analysis_audit.h
DISTFILES += fixtures/coco_analysis_reference.json fixtures/generate_coco_analysis_reference.py
DISTFILES += coco_bounds_smoke.cpp

SOURCES += odp_examples_audit.cpp
HEADERS += odp_examples_audit.h
DISTFILES += fixtures/odp_reference.json fixtures/generate_odp_reference.py

SOURCES += synthetic_audit.cpp
HEADERS += synthetic_audit.h
DISTFILES += fixtures/synthetic_reference.json fixtures/generate_synthetic_reference.py
