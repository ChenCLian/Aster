# Keep archive dependencies explicit: changing a module must relink its users.
contains(ASTER_MODULES, aster_desktop_ui):QT += network
LIBS += -L"$$OUT_PWD/../lib"
for(module, ASTER_MODULES) {
    LIBS += -l$$module
    win32-msvc {
        PRE_TARGETDEPS += $$OUT_PWD/../lib/$${module}.lib
    } else {
        PRE_TARGETDEPS += $$OUT_PWD/../lib/lib$${module}.a
    }
}
