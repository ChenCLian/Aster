# Project-local paths or environment variables; no hard-coded machine drives.
ASTER_ROOT = $$clean_path("$$PWD/..")
exists("$$ASTER_ROOT/config/local.pri"):include("$$ASTER_ROOT/config/local.pri")
DEFINES += _USE_MATH_DEFINES

win32 {
    # Resolve explicit project/local settings before consulting the environment.
    # In particular, an environment PREFIX must not override a local ROOT.
    isEmpty(VCPKG_PREFIX):!isEmpty(VCPKG_ROOT):VCPKG_PREFIX = $$VCPKG_ROOT/installed/x64-windows
    isEmpty(VCPKG_PREFIX):VCPKG_PREFIX = $$(VCPKG_PREFIX)
    isEmpty(VCPKG_PREFIX) {
        isEmpty(VCPKG_ROOT):VCPKG_ROOT = $$(VCPKG_ROOT)
        isEmpty(VCPKG_ROOT):error("Windows dependency path is not configured. Copy config/local.pri.example to config/local.pri and set VCPKG_ROOT or VCPKG_PREFIX; then rerun qmake.")
        VCPKG_PREFIX = $$VCPKG_ROOT/installed/x64-windows
    }
    !exists("$$VCPKG_PREFIX"):error("Dependency directory does not exist: $${VCPKG_PREFIX}. Set VCPKG_PREFIX in $${ASTER_ROOT}/config/local.pri to your existing installed/x64-windows directory; the inherited vcpkg root may have no installed libraries.")
}
include("$$PWD/numeric_backend.pri")

win32 {
    !exists("$$VCPKG_PREFIX/include/gsl/gsl_spline.h"):error("GSL header not found in $$VCPKG_PREFIX/include. Check the dependency path and gsl:x64-windows installation.")
    INCLUDEPATH += "$$VCPKG_PREFIX/include"
    CONFIG(debug, debug|release) {
        LIBS += -L"$$VCPKG_PREFIX/debug/lib" -lgsld -lgslcblasd
    } else {
        LIBS += -L"$$VCPKG_PREFIX/lib" -lgsl -lgslcblas
    }
}
unix {
    !isEmpty(GSL_PREFIX) {
        INCLUDEPATH += "$$GSL_PREFIX/include"
        isEmpty(GSL_LIB_DIR):GSL_LIB_DIR = $$GSL_PREFIX/lib
        LIBS += -L"$$GSL_LIB_DIR"
        QMAKE_RPATHDIR += "$$GSL_LIB_DIR"
        LIBS += -lgsl -lgslcblas
    } else:packagesExist(gsl) {
        CONFIG += link_pkgconfig
        PKGCONFIG += gsl
    } else {
        LIBS += -lgsl -lgslcblas
    }
    LIBS += -lpthread -lm
    !macx:LIBS += -ldl
}
