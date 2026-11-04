# Fixed numerical stack: LP64 LAPACK/OpenBLAS + bundled PocketFFT.
# Included after config/local.pri. No backend selection or optional component.
win32 {
    !win32-msvc:error(The supplied Windows portable configuration requires the MSVC x64 ABI)
    !equals(QT_ARCH, x86_64):error(The supplied vcpkg configuration uses x64-windows - other Windows architectures need matching dependencies)
    CONFIG(debug, debug|release):ASTER_NUMERIC_LIB_DIR = $$VCPKG_PREFIX/debug/lib
    else:ASTER_NUMERIC_LIB_DIR = $$VCPKG_PREFIX/lib
    isEmpty(OPENBLAS_LINK_NAME) {
        ASTER_OPENBLAS_CANDIDATES = openblas libopenblas openblasd libopenblasd
        for(candidate, ASTER_OPENBLAS_CANDIDATES) {
            exists("$$ASTER_NUMERIC_LIB_DIR/$${candidate}.lib") {
                OPENBLAS_LINK_NAME = $$candidate
                break()
            }
        }
    }
    isEmpty(LAPACK_LINK_NAME) {
        ASTER_LAPACK_CANDIDATES = lapack liblapack lapackd liblapackd
        for(candidate, ASTER_LAPACK_CANDIDATES) {
            exists("$$ASTER_NUMERIC_LIB_DIR/$${candidate}.lib") {
                LAPACK_LINK_NAME = $$candidate
                break()
            }
        }
    }
    isEmpty(OPENBLAS_LINK_NAME):error("OpenBLAS .lib not found in $${ASTER_NUMERIC_LIB_DIR}. Check config/local.pri and the selected Debug/Release configuration; install openblas:x64-windows only if it is missing.")
    isEmpty(LAPACK_LINK_NAME):error("LAPACK .lib not found in $${ASTER_NUMERIC_LIB_DIR}. Check config/local.pri and the selected Debug/Release configuration; install lapack-reference:x64-windows only if it is missing.")
    LIBS += -L"$$ASTER_NUMERIC_LIB_DIR" -l$$LAPACK_LINK_NAME -l$$OPENBLAS_LINK_NAME
} else:unix {
    # Default Unix/Homebrew OpenBLAS includes LAPACK. Override when the
    # distributor provides LAPACK separately; place LAPACK before BLAS.
    LIBS += $$ASTER_LAPACK_LIBS
    !isEmpty(OPENBLAS_ROOT) {
        LIBS += -L"$$OPENBLAS_ROOT/lib" -lopenblas
        QMAKE_RPATHDIR += "$$OPENBLAS_ROOT/lib"
    } else:packagesExist(openblas) {
        CONFIG += link_pkgconfig
        PKGCONFIG += openblas
    } else {
        LIBS += -lopenblas
    }
} else {
    error(No portable LAPACK build configuration for this platform)
}
