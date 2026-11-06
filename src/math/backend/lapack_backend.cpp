// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#include "math/backend/backend_detail.h"
#include <cmath>
#include <limits>
#include <mutex>

// Supported portable ABI: LP64 Netlib/gfortran LAPACK (vcpkg
// lapack-reference or the LAPACK bundled with Unix/Homebrew OpenBLAS).
// Keep ABI declarations confined to this adapter. The library must use
// LP64 integers and the documented Netlib/gfortran calling convention.
extern "C" {
void dstevr_(const char*, const char*, const int*, double*, double*,
             const double*, const double*, const int*, const int*,
             const double*, int*, double*, double*, const int*, int*,
             double*, const int*, int*, const int*, int*, std::size_t, std::size_t);
char* openblas_get_config();
int openblas_get_parallel();
void openblas_set_num_threads(int);
int openblas_get_num_threads();
}

namespace aster::numeric::backend::detail {
namespace {
struct Configuration {
    std::string description;
    bool serialize;
};
const Configuration& configuration()
{
    // C++ static initialization is synchronized; never change a process-wide
    // thread count on every task or from competing worker threads.
    static const Configuration config = [] {
        const char* raw = openblas_get_config();
        if (!raw) throw BackendError("OpenBLAS initialization", 0, "missing configuration");
        const std::string description(raw);
        if (description.find("USE64BITINT") != std::string::npos)
            throw BackendError("OpenBLAS initialization", 0, "ILP64 library; LP64 is required");
        openblas_set_num_threads(1);
        if (openblas_get_num_threads() != 1)
            throw BackendError("OpenBLAS initialization", 0, "could not select one internal thread");
        // Some single-thread OpenBLAS packages omit USE_LOCKING. Serializing
        // only native LAPACK calls avoids assuming such packages are reentrant.
        // FFT and the remainder of independent data tasks still run in parallel.
        return Configuration{description, openblas_get_parallel() == 0};
    }();
    return config;
}
std::mutex& lapackMutex() { static std::mutex mutex; return mutex; }
}

BackendInfo info()
{
    const auto& config = configuration();
    return {"portable", "LAPACK DSTEVR LP64; " + config.description,
            "PocketFFT (one thread)", 1, config.serialize};
}
Eigenpairs solve(std::vector<double>& diagonal, std::vector<double>& offDiagonal, std::size_t count)
{
    const auto& config = configuration();
    std::unique_lock<std::mutex> lock(lapackMutex(), std::defer_lock);
    if (config.serialize) lock.lock();
    const int n = static_cast<int>(diagonal.size());
    const int first = n - static_cast<int>(count) + 1, last = n;
    const char job = 'V', range = 'I';
    const double vl = 0, vu = 0, tolerance = -1;
    int found = 0, status = 0, query = -1, integerWorkspace = 0;
    double workspace = 0;
    Eigenpairs result;
    result.values.resize(n); // LAPACK specifies W with dimension N.
    result.vectors.resize(static_cast<std::size_t>(n) * count);
    std::vector<int> support(2 * static_cast<std::size_t>(n));
    dstevr_(&job, &range, &n, diagonal.data(), offDiagonal.data(), &vl, &vu,
            &first, &last, &tolerance, &found, result.values.data(), result.vectors.data(),
            &n, support.data(), &workspace, &query, &integerWorkspace, &query, &status, 1, 1);
    if (status || !std::isfinite(workspace) || workspace < 1 ||
        workspace > std::numeric_limits<int>::max() || integerWorkspace < 1)
        throw BackendError("LAPACK DSTEVR workspace query", status, "invalid workspace request");
    const int workCount = static_cast<int>(std::ceil(workspace));
    std::vector<double> work(workCount);
    std::vector<int> iwork(integerWorkspace);
    dstevr_(&job, &range, &n, diagonal.data(), offDiagonal.data(), &vl, &vu,
            &first, &last, &tolerance, &found, result.values.data(), result.vectors.data(),
            &n, support.data(), work.data(), &workCount, iwork.data(), &integerWorkspace, &status, 1, 1);
    if (status || found != static_cast<int>(count))
        throw BackendError("LAPACK DSTEVR", status, "requested eigenpairs were not produced");
    result.values.resize(count);
    return result;
}
} // namespace aster::numeric::backend::detail
