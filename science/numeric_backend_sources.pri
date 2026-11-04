# One numerical implementation, compiled only into the science library.
HEADERS += $$ASTER_ROOT/src/math/backend/numeric_backend.h \
           $$ASTER_ROOT/src/math/backend/backend_detail.h \
           $$ASTER_ROOT/src/third_party/pocketfft/pocketfft_hdronly.h
SOURCES += $$ASTER_ROOT/src/math/backend/numeric_backend.cpp \
           $$ASTER_ROOT/src/math/backend/lapack_backend.cpp \
           $$ASTER_ROOT/src/math/backend/pocketfft_backend.cpp
DISTFILES += $$ASTER_ROOT/src/third_party/pocketfft/LICENSE.md \
             $$ASTER_ROOT/src/third_party/pocketfft/README.md
