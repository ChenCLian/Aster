# Keep archive dependencies explicit: changing a module must relink its users.
contains(ASTER_MODULES, aster_desktop_ui):QT += network
isEmpty(ASTER_BUILD_ROOT):ASTER_BUILD_ROOT = $$clean_path($$OUT_PWD/..)
LIBS += -L"$$ASTER_BUILD_ROOT/lib"
for(module, ASTER_MODULES) {
    LIBS += -l$$module
    win32-msvc {
        PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/$${module}.lib
    } else {
        PRE_TARGETDEPS += $$ASTER_BUILD_ROOT/lib/lib$${module}.a
    }
}

contains(ASTER_MODULES, aster_desktop_ui):include($$PWD/archive.pri)
