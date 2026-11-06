// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/numeric_utils.h"
#include <algorithm>
#include <initializer_list>
#include <limits>
#include <stdexcept>

size_t next_fast_len(size_t n) {

    if (n > std::numeric_limits<size_t>::max() / 4)
        throw std::length_error("FFT padding length overflows size_t");
    size_t target = std::max(n * 4, (size_t)512);

    n = target;

    while (true) {
        size_t m = n;
        for (size_t p : {2, 3, 5}) {
            while (m % p == 0) {
                m /= p;
            }
        }
        if (m == 1)
            return n;

        if (n == std::numeric_limits<size_t>::max())
            throw std::length_error("No representable FFT padding length");
        ++n;
    }
}
