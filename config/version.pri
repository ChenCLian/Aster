# The only editable application version is the repository-root VERSION file.
ASTER_VERSION = $$cat($$PWD/../VERSION, lines)
isEmpty(ASTER_VERSION):error(Missing application VERSION)
VERSION = $$section(ASTER_VERSION, -, 0, 0)
# Reading a file with cat() alone does not make it a qmake dependency.
QMAKE_INTERNAL_INCLUDED_FILES += $$clean_path($$PWD/../VERSION)
# A changed -D flag alone need not rebuild main.o. Generate a real header so
# every consumer is recompiled when the complete version (including beta) changes.
aster_version_header.input = $$PWD/aster_version.h.in
aster_version_header.output = $$OUT_PWD/aster_version.h
QMAKE_SUBSTITUTES += aster_version_header
INCLUDEPATH += $$OUT_PWD
