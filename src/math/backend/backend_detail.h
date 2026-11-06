// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_BACKEND_DETAIL_H
#define ASTER_MATH_BACKEND_DETAIL_H
#include "math/backend/numeric_backend.h"

// Private implementation contract. Exactly one LAPACK provider and one FFT
// provider are linked by science/numeric_backend_sources.pri.
namespace aster::numeric::backend::detail {
BackendInfo info();
Eigenpairs solve(std::vector<double>& diagonal, std::vector<double>& offDiagonal,
                 std::size_t count);
std::vector<std::complex<double>> forward(std::vector<double>& input,
                                         std::size_t length, std::size_t batches);
std::vector<double> backward(std::vector<std::complex<double>>& input,
                             std::size_t length, std::size_t batches);
} // namespace aster::numeric::backend::detail
#endif
