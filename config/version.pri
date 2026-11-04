# The only editable application version is the repository-root VERSION file.
ASTER_VERSION = $$cat($$PWD/../VERSION, lines)
isEmpty(ASTER_VERSION):error(Missing application VERSION)
VERSION = $$section(ASTER_VERSION, -, 0, 0)
DEFINES += ASTER_VERSION=\\\"$${ASTER_VERSION}\\\"
