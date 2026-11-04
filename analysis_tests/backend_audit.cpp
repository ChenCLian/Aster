#include "backend_audit.h"
#include "processing/science_helpers.h"
#include "math/backend/numeric_backend.h"
#include "math/numeric_utils.h"
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <future>
#include <limits>

namespace b = aster::numeric::backend;
namespace {
constexpr double pi = 3.14159265358979323846;
std::vector<double> data(std::size_t n, std::size_t rows)
{
    std::vector<double> x(n * rows);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = std::sin(.37 * i) + .4 * std::cos(.19 * i) + .01 * i;
    return x;
}
double error(const std::vector<double>& a, const std::vector<double>& reference)
{
    if (a.size() != reference.size()) return std::numeric_limits<double>::infinity();
    double maxError = 0, scale = 1;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!std::isfinite(a[i])) return std::numeric_limits<double>::infinity();
        maxError = std::max(maxError, std::abs(a[i] - reference[i]));
        scale = std::max(scale, std::abs(reference[i]));
    }
    return maxError / scale;
}
class BackendAudit : public QObject {
    Q_OBJECT
private slots:
    void providerInformation();
    void fftRoundtrip_data();
    void fftRoundtrip();
    void directDft();
    void selectedEigenpairs();
    void repeatedEigenvaluesAndScalar();
    void rejectsInvalidDimensions();
    void rejectsNonFiniteAndComplexEndpoints();
    void independentConcurrentCalls();
    void nativeErrorCarriesStatus();
    void paddingRejectsIntegerOverflow();
};
void BackendAudit::providerInformation()
{
    const auto info = b::backendInfo();
    QCOMPARE(info.name, std::string("portable"));
    QVERIFY(!info.linearAlgebra.empty()); QVERIFY(!info.fft.empty());
    QCOMPARE(info.threadsPerCall, 1);
    const auto provenance=aster::science_detail::numericBackendReport();
    QCOMPARE(provenance["provider"].toString(),QString::fromStdString(info.name));
    QCOMPARE(provenance["threads_per_call"].toInt(),1);
    qInfo().noquote() << QString::fromStdString(info.name + " / " + info.linearAlgebra + " / " + info.fft)
                      << "serialized LAPACK:" << info.serializesLapackCalls;
}
void BackendAudit::paddingRejectsIntegerOverflow()
{
    QVERIFY_EXCEPTION_THROWN(next_fast_len(std::numeric_limits<size_t>::max()), std::length_error);
    QCOMPARE(next_fast_len(0), size_t(512));
    const auto n = next_fast_len(1000);
    QVERIFY(n >= 4000);
    auto rest = n;
    for (size_t p : {2, 3, 5}) while (rest % p == 0) rest /= p;
    QCOMPARE(rest, size_t(1));
}
void BackendAudit::fftRoundtrip_data()
{
    QTest::addColumn<int>("length"); QTest::addColumn<int>("rows");
    for (int n : {1, 2, 7, 16, 127, 256, 257, 1536})
        for (int batches : {1, 3})
            QTest::newRow(qPrintable(QString("n%1_rows%2").arg(n).arg(batches))) << n << batches;
}
void BackendAudit::fftRoundtrip()
{
    QFETCH(int, length); QFETCH(int, rows);
    auto input = data(length, rows); const auto original = input;
    auto transformed = b::realFft(input, length, rows); const auto saved = transformed;
    const auto output = b::inverseRealFft(transformed, length, rows);
    QVERIFY(input == original); QVERIFY(transformed == saved);
    QVERIFY(error(output, original) < 2e-12);
}
void BackendAudit::directDft()
{
    constexpr int n = 17, rows = 3, bins = n / 2 + 1;
    const auto input = data(n, rows);
    const auto actual = b::realFft(input, n, rows);
    for (int row = 0; row < rows; ++row) for (int k = 0; k < bins; ++k) {
        std::complex<double> expected;
        for (int j = 0; j < n; ++j)
            expected += input[row * n + j] * std::polar(1.0, -2 * pi * k * j / n);
        QVERIFY(std::abs(actual[row * bins + k] - expected) < 2e-11);
    }
}
void BackendAudit::selectedEigenpairs()
{
    constexpr int n = 19, count = 4;
    const std::vector<double> diagonal(n, 2), off(n - 1, 1);
    const auto result = b::largestTridiagonalEigenpairs(diagonal, off, count);
    QCOMPARE(result.values.size(), std::size_t(count));
    QCOMPARE(result.vectors.size(), std::size_t(n * count));
    for (int j = 0; j < count; ++j) {
        const auto expected = 2 + 2 * std::cos(pi * (count - j) / (n + 1));
        QVERIFY(std::abs(result.values[j] - expected) < 2e-12);
        for (int i = 0; i < n; ++i) {
            const auto* v = result.vectors.data() + j * n;
            const double av = 2 * v[i] + (i ? v[i-1] : 0) + (i+1<n ? v[i+1] : 0);
            QVERIFY(std::abs(av - result.values[j] * v[i]) < 2e-12);
        }
        for (int k = 0; k <= j; ++k) {
            double dot = 0;
            for (int i = 0; i < n; ++i) dot += result.vectors[j*n+i] * result.vectors[k*n+i];
            QVERIFY(std::abs(dot - (j == k ? 1 : 0)) < 2e-11);
        }
    }
    QVERIFY(diagonal == std::vector<double>(n, 2));
    QVERIFY(off == std::vector<double>(n - 1, 1));
}
void BackendAudit::repeatedEigenvaluesAndScalar()
{
    const auto scalar = b::largestTridiagonalEigenpairs({-3}, {}, 1);
    QCOMPARE(scalar.values.front(), -3.0); QCOMPARE(scalar.vectors.front(), 1.0);
    const auto repeated = b::largestTridiagonalEigenpairs({2, 2, 2, 2}, {0, 0, 0}, 4);
    for (double value : repeated.values) QVERIFY(std::abs(value - 2) < 1e-12);
    for (int j = 0; j < 4; ++j) for (int k = 0; k <= j; ++k) {
        double dot = 0;
        for (int i = 0; i < 4; ++i) dot += repeated.vectors[j*4+i] * repeated.vectors[k*4+i];
        QVERIFY(std::abs(dot - (j == k ? 1 : 0)) < 1e-12);
    }
}
void BackendAudit::rejectsInvalidDimensions()
{
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::realFft({}, 0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::realFft({1}, 1, 0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::realFft({1}, 4));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::realFft({}, std::numeric_limits<std::size_t>::max()));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::inverseRealFft({}, 3));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({}, {}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({1, 2}, {}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({1, 2}, {1}, 0));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({1, 2}, {1}, 3));
}
void BackendAudit::rejectsNonFiniteAndComplexEndpoints()
{
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto inf = std::numeric_limits<double>::infinity();
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::realFft({nan}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::inverseRealFft({{inf, 0}}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::inverseRealFft({{0, 1}}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::inverseRealFft({{0, 0}, {0, 1}}, 2));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({nan}, {}, 1));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, b::largestTridiagonalEigenpairs({1, 2}, {inf}, 1));
}
void BackendAudit::independentConcurrentCalls()
{
    auto run = [](int seed) {
        const int n = 41 + seed;
        std::vector<double> d(n, seed + 2), e(n - 1, .5);
        auto eig = b::largestTridiagonalEigenpairs(std::move(d), std::move(e), 3);
        auto input = data(n, 3);
        auto restored = b::inverseRealFft(b::realFft(input, n, 3), n, 3);
        if (error(restored, input) > 2e-12) throw std::runtime_error("Concurrent FFT mismatch");
        return eig.values;
    };
    std::vector<std::vector<double>> expected;
    for (int i = 0; i < 8; ++i) expected.push_back(run(i));
    std::vector<std::future<std::vector<double>>> jobs;
    for (int i = 0; i < 8; ++i) jobs.push_back(std::async(std::launch::async, run, i));
    for (int i = 0; i < 8; ++i) QVERIFY(error(jobs[i].get(), expected[i]) < 1e-12);
}
void BackendAudit::nativeErrorCarriesStatus()
{
    const b::BackendError error("DSTEVR", 7, "did not converge");
    QCOMPARE(error.operation(), std::string("DSTEVR")); QCOMPARE(error.status(), 7);
    QVERIFY(QString::fromUtf8(error.what()).contains("did not converge"));
}
}
int runBackendAudit(int argc, char** argv)
{
    BackendAudit tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "backend_audit.moc"
