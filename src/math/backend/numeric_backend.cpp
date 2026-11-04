#include "math/backend/backend_detail.h"
#include <cmath>
#include <limits>
#include <utility>

namespace aster::numeric::backend {
namespace {
std::size_t checkedElements(std::size_t rows, std::size_t columns, std::size_t elementBytes)
{
    // LP64 provider counts and byte strides must be representable BEFORE any
    // narrowing conversion or native call. Algorithm-level budgets remain in
    // the caller (MTM currently checks its simultaneous workspace estimate).
    constexpr auto maxInt = static_cast<std::size_t>(std::numeric_limits<int>::max());
    const auto maxBytes = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (!rows || !columns || rows > maxInt || columns > maxInt ||
        rows > maxBytes / elementBytes / columns)
        throw std::invalid_argument("Numeric backend dimensions exceed LP64 or address limits");
    return rows * columns;
}
void finiteInput(const std::vector<double>& values)
{
    for (double v : values)
        if (!std::isfinite(v)) throw std::invalid_argument("Non-finite numeric backend input");
}
void finiteOutput(const std::vector<double>& values, const char* operation)
{
    for (double v : values)
        if (!std::isfinite(v)) throw BackendError(operation, 0, "non-finite output; rescale the input");
}
}
BackendError::BackendError(const std::string& operation, int status, const std::string& detail)
    : std::runtime_error(operation + " failed (status " + std::to_string(status) + "): " + detail),
      m_operation(operation), m_status(status)
{}

BackendInfo backendInfo() {
    return detail::info();
}

Eigenpairs largestTridiagonalEigenpairs(std::vector<double> diagonal,
                                       std::vector<double> offDiagonal, std::size_t count)
{
    const auto n = diagonal.size();
    if (!n || offDiagonal.size() != n - 1 || !count || count > n)
        throw std::invalid_argument("Invalid symmetric tridiagonal eigensolver dimensions");
    const auto elements = checkedElements(n, count, sizeof(double));
    // DSTEVR's minimum WORK length is 20*N (32-bit LP64 integer).
    if (n > static_cast<std::size_t>(std::numeric_limits<int>::max()) / 20)
        throw std::invalid_argument("Tridiagonal workspace exceeds LP64 limits");
    finiteInput(diagonal); finiteInput(offDiagonal);
    if (n == 1) return {{diagonal.front()}, {1.0}};
    auto result = detail::solve(diagonal, offDiagonal, count);
    if (result.values.size() != count || result.vectors.size() != elements)
        throw BackendError("tridiagonal eigensolver", 0, "unexpected output dimensions");
    finiteOutput(result.values, "tridiagonal eigensolver");
    finiteOutput(result.vectors, "tridiagonal eigensolver");
    return result;
}

std::vector<std::complex<double>> realFft(std::vector<double> input,
                                         std::size_t length, std::size_t batches)
{
    const auto expected = checkedElements(batches, length, sizeof(double));
    const auto outputCount = checkedElements(batches, length / 2 + 1, sizeof(std::complex<double>));
    if (input.size() != expected) throw std::invalid_argument("Real FFT input shape mismatch");
    finiteInput(input);
    auto result = detail::forward(input, length, batches);
    if (result.size() != outputCount) throw BackendError("real FFT", 0, "unexpected output dimensions");
    for (const auto& v : result)
        if (!std::isfinite(v.real()) || !std::isfinite(v.imag()))
            throw BackendError("real FFT", 0, "non-finite output; rescale the input");
    return result;
}

std::vector<double> inverseRealFft(std::vector<std::complex<double>> input,
                                  std::size_t length, std::size_t batches)
{
    const auto outputCount = checkedElements(batches, length, sizeof(double));
    const auto bins = length / 2 + 1;
    const auto expected = checkedElements(batches, bins, sizeof(std::complex<double>));
    if (input.size() != expected) throw std::invalid_argument("Inverse real FFT input shape mismatch");
    for (const auto& v : input)
        if (!std::isfinite(v.real()) || !std::isfinite(v.imag()))
            throw std::invalid_argument("Non-finite inverse real FFT input");
    for (std::size_t row = 0; row < batches; ++row) {
        if (input[row * bins].imag() != 0 ||
            (length % 2 == 0 && input[row * bins + bins - 1].imag() != 0))
            throw std::invalid_argument("Real FFT DC/Nyquist bins must be real");
    }
    auto result = detail::backward(input, length, batches);
    if (result.size() != outputCount) throw BackendError("inverse real FFT", 0, "unexpected output dimensions");
    finiteOutput(result, "inverse real FFT");
    return result;
}
} // namespace aster::numeric::backend
