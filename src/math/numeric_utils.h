#ifndef ASTER_MATH_NUMERIC_UTILS_H
#define ASTER_MATH_NUMERIC_UTILS_H
#include <cstddef>
// Existing MTM padding policy: first 2/3/5-smooth size >= max(4*n, 512).
// Explicit Task NFFT bypasses this policy for the final spectrum.
size_t next_fast_len(size_t n);
#endif // ASTER_MATH_NUMERIC_UTILS_H
