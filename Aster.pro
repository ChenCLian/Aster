TEMPLATE = subdirs
CONFIG += ordered
CONFIG -= debug_and_release debug_and_release_target

SUBDIRS += core tests batch_tests
tests.depends = core
batch_tests.depends = core
!contains(CONFIG, core_only) {
    SUBDIRS += io science application analysis_tests cross_spectrum_tests
    io.depends = core
    science.depends = core io
    application.depends = core io
    analysis_tests.depends = core io science application
    cross_spectrum_tests.depends = core io science
}
!contains(CONFIG, core_only):!contains(CONFIG, no_gui) {
    SUBDIRS += desktop_ui desktop ui_tests joint_ui_tests rewrite_tests linked_input_tests style_export_tests workspace_tests integration_tests update_tests
    desktop_ui.depends = core io application
    desktop.depends = desktop_ui application science io core
    joint_ui_tests.depends = desktop_ui application science io core
    linked_input_tests.depends = desktop_ui application science io core
    rewrite_tests.depends = desktop_ui application science io core
    ui_tests.depends = desktop_ui application science io core
    style_export_tests.depends = desktop_ui application science io core
    workspace_tests.depends = desktop_ui application science io core
    integration_tests.depends = desktop_ui application science io core
    update_tests.depends = desktop_ui application science io core
}
# Optional descriptor exporter for generated parameter references.
contains(CONFIG, help_reference):!contains(CONFIG, core_only) {
    SUBDIRS += help_reference
    help_reference.file = tools/help_reference/catalog.pro
    help_reference.depends = core io science
}

# Optional validation of the current registered AR1 global test.
contains(CONFIG, calibration):!contains(CONFIG, core_only) {
    SUBDIRS += ar1_global_validation
    ar1_global_validation.file = tools/ar1_global_validation/validation.pro
    ar1_global_validation.depends = core io science
}

DISTFILES += LICENSE COPYRIGHT.md SOURCE_CODE.md licenses/GPL-3.0.txt licenses/AstroGeoFit_LICENSE.txt
DISTFILES += README.md AUTHORS.md RELEASE_NOTES.md PACKAGE_STATUS.json PACKAGE_MANIFEST.json .clang-format VERSION .gitignore
DISTFILES += config/version.pri config/aster_version.h.in
DISTFILES += config/local.pri.example config/scientific_dependencies.pri config/link_modules.pri
DISTFILES += config/numeric_backend.pri science/numeric_backend_sources.pri
DISTFILES += $$files($$PWD/docs/*.md, true) $$files($$PWD/docs/*.json, true)
DISTFILES += $$files($$PWD/tools/*.md, true) $$files($$PWD/tools/*.py, true)
DISTFILES += $$files($$PWD/tools/*.m, true) $$files($$PWD/tools/*.m.in, true)
DISTFILES += $$files($$PWD/tools/*.json, true)
DISTFILES += tools/peak_validation/reference_bridge.cpp
DISTFILES += tools/help_reference/catalog.pro tools/help_reference/main.cpp
DISTFILES += tools/ar1_global_validation/validation.pro tools/ar1_global_validation/main.cpp
DISTFILES += tools/ar1_global_validation/portable_random.h
DISTFILES += resources/astronomy/solutions/La2010_README.TXT resources/astronomy/solutions/PT_README.txt

DISTFILES += tools/release/package_windows.ps1 $$files($$PWD/tools/release/*.sh)
DISTFILES += .github/workflows/build-desktop.yml
DISTFILES += resources/app/aster-256.png
