# Shared archive reader for full-package downloads. No platform install helper.
isEmpty(ASTER_ARCHIVE_CONFIGURED) {
    ASTER_ARCHIVE_CONFIGURED = 1
    exists($$PWD/local.pri):include($$PWD/local.pri)
    win32 {
        isEmpty(VCPKG_PREFIX):!isEmpty(VCPKG_ROOT):VCPKG_PREFIX = $$VCPKG_ROOT/installed/x64-windows
        isEmpty(VCPKG_PREFIX):VCPKG_PREFIX = $$(VCPKG_PREFIX)
        isEmpty(VCPKG_ROOT):VCPKG_ROOT = $$(VCPKG_ROOT)
        isEmpty(VCPKG_PREFIX):!isEmpty(VCPKG_ROOT):VCPKG_PREFIX = $$VCPKG_ROOT/installed/x64-windows
        !exists($$VCPKG_PREFIX/include/archive.h):error("Install libarchive:x64-windows in your configured vcpkg prefix, then rerun qmake.")
        INCLUDEPATH += $$VCPKG_PREFIX/include
        CONFIG(debug, debug|release) { LIBS += -L"$$VCPKG_PREFIX/debug/lib" -larchive } else { LIBS += -L"$$VCPKG_PREFIX/lib" -larchive }
    } else {
        !isEmpty(ARCHIVE_PREFIX) {
            INCLUDEPATH += $$ARCHIVE_PREFIX/include
            LIBS += -L"$$ARCHIVE_PREFIX/lib" -larchive
            QMAKE_RPATHDIR += $$ARCHIVE_PREFIX/lib
        } else { LIBS += -larchive }
    }
}
