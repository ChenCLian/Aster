/* Optional GNU/LLVM ELF linker fault-injection check; not a runtime dependency.
   From the source root:
   cc -std=c11 -Isrc/third_party/wavelib analysis_tests/native_cwt_allocations.c \
      src/third_party/wavelib/cwt.c src/third_party/wavelib/cwtmath.c \
      src/third_party/wavelib/hsfft.c -Wl,--wrap=malloc -Wl,--wrap=free -lm -o /tmp/aster_cwt_allocations
   /tmp/aster_cwt_allocations
*/
#include "cwt.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t n);
void __real_free(void *p);
static int active, count, fail_at, live;
static void *pointers[32];
void *__wrap_malloc(size_t n)
{
    void *p;
    int i;
    if (active && ++count == fail_at) return NULL;
    p = __real_malloc(n);
    if (active && p) {
        for (i = 0; i < 32 && pointers[i]; ++i) {}
        assert(i < 32); pointers[i] = p; ++live;
    }
    return p;
}
void __wrap_free(void *p)
{
    int i;
    if (p) for (i = 0; i < 32; ++i) if (pointers[i] == p) {
        pointers[i] = NULL; --live; break;
    }
    __real_free(p);
}
int main(void)
{
    double input[8] = {0, 1, 0, -1, 0, 1, 0, -1};
    double wave[16], scale[1] = {2}, period[1], coi[8];
    int status, fail;
    for (fail = 1; fail <= 6; ++fail) {
        count = 0; fail_at = fail; active = 1;
        status = cwavelet_checked(input, 8, 1, 0, 6, 2, .1, 1, 16,
                                  wave, scale, period, coi);
        active = 0;
        assert(status == -1); assert(live == 0);
    }
    count = 0; fail_at = 0; active = 1;
    status = cwavelet_checked(input, 8, 1, 0, 6, 2, .1, 1, 16,
                              wave, scale, period, coi);
    active = 0;
    assert(status == 0); assert(count == 6); assert(live == 0);
    for (int i = 0; i < 16; ++i) assert(isfinite(wave[i]));
    assert(period[0] > 0);
    assert(cwavelet_checked(input, 8, 1, 0, 6, 2, .1, 1, 4,
                            wave, scale, period, coi) == -2);
    puts("8 checks passed: six allocation failures, successful transform, invalid padding; no tracked leaks");
    return 0;
}
