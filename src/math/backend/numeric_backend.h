// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_NUMERIC_BACKEND_H
#define ASTER_MATH_NUMERIC_BACKEND_H

#include <complex>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// This is the only backend header algorithms should include. No Qt, vendor
// headers, vendor integer types, or process-wide mutable algorithm parameters.
namespace aster::numeric::backend {
struct BackendInfo {
    std::string name;
    std::string linearAlgebra;
    std::string fft;
    int threadsPerCall = 1;
    bool serializesLapackCalls = false;
};

class BackendError : public std::runtime_error {
public:
    BackendError(const std::string& operation, int status, const std::string& detail);
    const std::string& operation() const noexcept { return m_operation; }
    int status() const noexcept { return m_status; }
private:
    std::string m_operation;
    int m_status;
};

struct Eigenpairs {
    // Selected eigenvalues in ascending order. Each corresponding normalized
    // eigenvector occupies one contiguous column: vectors[column * n + row].
    std::vector<double> values;
    std::vector<double> vectors;
};

BackendInfo backendInfo();

// Largest count eigenpairs of a REAL SYMMETRIC TRIDIAGONAL matrix.
// diagonal.size() = n, offDiagonal.size() = n-1. No dense n*n allocation.
// Arguments are owned copies: use std::move for disposable work buffers.
Eigenpairs largestTridiagonalEigenpairs(std::vector<double> diagonal,
                                       std::vector<double> offDiagonal,
                                       std::size_t count);

// Contiguous batches. Forward is unscaled; inverse includes 1/length.
// Real input/output: batches * length. Complex: batches * (length/2 + 1).
// Inverse takes a real-signal half spectrum; DC and even-length Nyquist must
// have zero imaginary part. Odd and even lengths are both supported.
// Owned buffers keep vendor routines from modifying callers' lvalue inputs.
std::vector<std::complex<double>> realFft(std::vector<double> input,
                                         std::size_t length, std::size_t batches = 1);
std::vector<double> inverseRealFft(std::vector<std::complex<double>> input,
                                  std::size_t length, std::size_t batches = 1);
} // namespace aster::numeric::backend
#endif
