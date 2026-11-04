#include "math/backend/backend_detail.h"
// No shared mutable FFT plans or internal thread pool. Aster parallelizes data
// records, and each call owns its buffers and transform plan.
#define POCKETFFT_NO_MULTITHREADING
#define POCKETFFT_CACHE_SIZE 0
#include "third_party/pocketfft/pocketfft_hdronly.h"

namespace aster::numeric::backend::detail {
std::vector<std::complex<double>> forward(std::vector<double>& input,
                                         std::size_t length, std::size_t batches)
{
    const auto bins = length / 2 + 1;
    std::vector<std::complex<double>> output(bins * batches);
    pocketfft::r2c<double>({batches, length},
        {static_cast<std::ptrdiff_t>(length * sizeof(double)), sizeof(double)},
        {static_cast<std::ptrdiff_t>(bins * sizeof(std::complex<double>)), sizeof(std::complex<double>)},
        std::size_t(1), true, input.data(), output.data(), 1.0, 1);
    return output;
}
std::vector<double> backward(std::vector<std::complex<double>>& input,
                             std::size_t length, std::size_t batches)
{
    const auto bins = length / 2 + 1;
    std::vector<double> output(length * batches);
    pocketfft::c2r<double>({batches, length},
        {static_cast<std::ptrdiff_t>(bins * sizeof(std::complex<double>)), sizeof(std::complex<double>)},
        {static_cast<std::ptrdiff_t>(length * sizeof(double)), sizeof(double)},
        std::size_t(1), false, input.data(), output.data(), 1.0 / length, 1);
    return output;
}
} // namespace aster::numeric::backend::detail
