QT = core sql
TEMPLATE = lib
CONFIG += staticlib c++17
CONFIG -= app_bundle debug_and_release debug_and_release_target
TARGET = aster_io
DESTDIR = $$OUT_PWD/../lib
INCLUDEPATH += $$PWD/../src
win32-msvc:QMAKE_CXXFLAGS += /utf-8
SOURCES += ../src/io/files/series_file_reader.cpp \
    ../src/io/database/data_codec.cpp ../src/io/database/result_repository.cpp
HEADERS += ../src/io/files/series_file_reader.h \
    ../src/io/database/data_codec.h ../src/io/database/result_repository.h

SOURCES += ../src/io/exports/csv_export.cpp
HEADERS += ../src/io/exports/csv_export.h
SOURCES += ../src/io/exports/export_path.cpp
HEADERS += ../src/io/exports/export_path.h

SOURCES += ../src/io/database/peak_index.cpp
HEADERS += ../src/io/database/record_filter.h ../src/io/database/peak_index.h

HEADERS += ../src/io/database/output_reference.h

SOURCES += ../src/io/database/plot_reader.cpp
HEADERS += ../src/io/database/plot_reader.h ../src/io/database/plot_request.h
HEADERS += ../src/io/database/grid_sampling.h

HEADERS += ../src/io/database/data_format.h

SOURCES += ../src/io/astronomy/la2004_reader.cpp
HEADERS += ../src/io/astronomy/la2004_reader.h ../src/math/astronomy/orbital_elements.h
RESOURCES += ../resources/astronomy/astronomy.qrc
DISTFILES += ../resources/astronomy/la2004/README.TXT
SOURCES += ../src/io/astronomy/orbital_table.cpp
HEADERS += ../src/io/astronomy/orbital_table.h
RESOURCES += ../resources/astronomy/orbital_solutions.qrc

SOURCES += ../src/io/database/record_selection.cpp
HEADERS += ../src/io/database/record_query.h

SOURCES += ../src/io/database/sqlite_snapshot.cpp ../src/io/database/database_transfer.cpp
HEADERS += ../src/io/database/sqlite_snapshot.h ../src/io/database/database_transfer.h

SOURCES += ../src/io/examples/example_catalog.cpp
HEADERS += ../src/io/examples/example_catalog.h
RESOURCES += ../resources/examples/examples.qrc
DISTFILES += ../resources/examples/LICENSE.txt
