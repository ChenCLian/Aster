#include "io/database/plot_reader.h"
#include <QDataStream>
#include <QIODevice>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonArray>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace aster
{
namespace
{
void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
// SQL substring keeps Qt-side allocations bounded and works with Qt's bundled SQLite
// on all platforms; no dependency on a second SQLite build/ABI. Each query remains
// inside the caller's transaction. SQLite's own page/value cache is driver-managed.
class Blob : public QIODevice
{
  public:
    Blob(QSqlDatabase db, qint64 id, CancellationToken cancel)
        : m_db(db), m_id(id), m_cancel(cancel)
    {
        QSqlQuery q(db);
        q.prepare("SELECT length(data) FROM aster_outputs WHERE rowid=?");
        q.addBindValue(id);
        require(q.exec() && q.next(), "Plot output disappeared");
        m_size = q.value(0).toLongLong();
        require(m_size >= 7 && m_size <= DataFormat::byteLimit, "Invalid plot payload size");
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }
    qint64 size() const override
    {
        return m_size;
    }
    bool isSequential() const override
    {
        return false;
    }
    qint64 readAt(qint64 offset, char *target, qint64 length)
    {
        require(!m_cancel.isCancelled(), "Plot preparation cancelled");
        require(offset >= 0 && length >= 0 && offset <= m_size - length, "Truncated plot data");
        qint64 done = 0;
        while (done < length)
        {
            const auto at = offset + done;
            if (at < m_start || at >= m_start + m_cache.size())
            {
                QSqlQuery q(m_db);
                q.prepare("SELECT substr(data,?,?) FROM aster_outputs WHERE rowid=?");
                q.addBindValue(at + 1);
                q.addBindValue(std::min<qint64>(PlotLimits::chunkBytes, m_size - at));
                q.addBindValue(m_id);
                require(q.exec() && q.next(), "Cannot read plot data");
                m_cache = q.value(0).toByteArray();
                m_start = at;
                bytes += m_cache.size();
                require(!m_cache.isEmpty(), "Truncated plot data");
            }
            auto n = std::min<qint64>(length - done, m_cache.size() - (at - m_start));
            std::memcpy(target + done, m_cache.constData() + at - m_start, size_t(n));
            done += n;
        }
        return done;
    }
    qint64 bytes = 0;

  protected:
    qint64 readData(char *target, qint64 length) override
    {
        return readAt(pos(), target, std::min(length, m_size - pos()));
    }
    qint64 writeData(const char *, qint64) override
    {
        return -1;
    }

  private:
    QSqlDatabase m_db;
    qint64 m_id, m_size = 0, m_start = -1;
    QByteArray m_cache;
    CancellationToken m_cancel;
};
struct Array
{
    qint64 offset = 0;
    quint32 count = 0;
    // Per-array cache avoids x/y alternation thrashing the blob cache.
    mutable QByteArray cache;
    mutable qint64 first = -1;
    double at(Blob &b, qint64 index) const
    {
        require(index >= 0 && index < count, "Invalid plot array index");
        if (index < first || index >= first + cache.size() / 8)
        {
            first = index;
            cache.resize(std::min<qint64>(PlotLimits::chunkBytes, (count - index) * 8));
            b.readAt(offset + index * 8, cache.data(), cache.size());
        }
        quint64 bits = qFromLittleEndian<quint64>(
            reinterpret_cast<const uchar *>(cache.constData() + (index - first) * 8));
        double value;
        std::memcpy(&value, &bits, 8);
        require(std::isfinite(value), "Plot data contains non-finite values");
        return value;
    }
};
Array array(QDataStream &s, Blob &b, quint64 &budget)
{
    quint32 n = 0;
    s >> n;
    budget += n;
    require(budget <= PlotLimits::maxNumbers && qint64(n) * 8 <= b.size() - b.pos(),
            "Invalid array length");
    Array a;
    a.offset = b.pos();
    a.count = n;
    require(b.seek(a.offset + qint64(n) * 8), "Invalid array offset");
    return a;
}
QString string(QDataStream &s)
{
    quint32 n = 0;
    s >> n;
    require(n <= DataFormat::textLimit, "Invalid text length");
    QByteArray bytes(n, Qt::Uninitialized);
    require(s.readRawData(bytes.data(), n) == int(n), "Truncated text");
    return QString::fromUtf8(bytes);
}
AxisInfo axis(QDataStream &s)
{
    auto name = string(s);
    return {name, string(s)};
}
struct Point
{
    double x = 0, y = 0;
    qint64 index = -1;
};
struct Bucket
{
    Point first, last, low, high;
    void add(Point p)
    {
        if (first.index < 0)
        {
            first = last = low = high = p;
            return;
        }
        last = p;
        if (p.y < low.y)
            low = p;
        if (p.y > high.y)
            high = p;
    }
};
QPair<qint64, qint64> interval(Blob &b, const Array &a, bool window, double lo, double hi)
{
    require(a.count > 0, "Empty grid axis");
    double previous = a.at(b, 0);
    qint64 first = 0, last = a.count;
    if (window)
    {
        first = a.count;
        last = 0;
    }
    for (qint64 i = 0; i < a.count; ++i)
    {
        const auto v = a.at(b, i);
        require(!i || v > previous, "Grid axes must be strictly increasing");
        previous = v;
        if (window && v >= lo && v <= hi)
        {
            first = std::min(first, i);
            last = i + 1;
        }
    }
    if (window && first == a.count)
    {
        qint64 l = 0, h = a.count;
        while (l < h)
        {
            auto m = (l + h) / 2;
            if (a.at(b, m) < lo)
                l = m + 1;
            else
                h = m;
        }
        first = std::clamp<qint64>(l, 0, a.count - 1);
        last = first + 1;
    }
    return {std::max<qint64>(0, first - 1), std::min<qint64>(a.count, last + 1)};
}
} // namespace
NodeResult readPlotBlob(QSqlDatabase db, qint64 rowId, const QString &kind,
                        const PlotRequest &request, const CancellationToken &cancel)
{
    try
    {
        Blob b(db, rowId, cancel);
        QDataStream s(&b);
        s.setVersion(QDataStream::Qt_6_0);
        s.setByteOrder(QDataStream::LittleEndian);
        s.setFloatingPointPrecision(QDataStream::DoublePrecision);
        quint32 magic;
        quint16 version;
        quint8 type;
        s >> magic >> version >> type;
        require(magic == DataFormat::magic && version == DataFormat::version,
                "Unknown plot encoding");
        require(type >= 1 && type <= 4 &&
                    QStringList{"Series", "Spectrum", "Grid", "Table"}[type - 1] == kind,
                "Plot kind mismatch");
        const int width = std::clamp(request.columns, 32, PlotLimits::maxDimension);
        const int height = std::clamp(request.rows, 32, PlotLimits::maxDimension);
        require(!request.window ||
                    (std::isfinite(request.xLower) && std::isfinite(request.xUpper) &&
                     request.xLower < request.xUpper && std::isfinite(request.yLower) &&
                     std::isfinite(request.yUpper) && request.yLower < request.yUpper),
                "Invalid plot viewport");
        quint64 budget = 0;
        DataRef data;
        bool reduced = false;
        QJsonArray bounds;
        if (type == 1 || type == 2)
        {
            auto xa = axis(s), ya = axis(s);
            auto x = array(s, b, budget), y = array(s, b, budget);
            require(x.count && x.count == y.count, "Unequal plot array lengths");
            double xmin = std::numeric_limits<double>::infinity(), xmax = -xmin, ymin = xmin,
                   ymax = -xmin;
            // The first pass bounds memory, including unsorted input. Subsequent viewport
            // passes still scan x/y, preserving extrema rather than dropping unseen spikes.
            if (!request.window)
                for (qint64 i = 0; i < x.count; ++i)
                {
                    auto v = x.at(b, i);
                    xmin = std::min(xmin, v);
                    xmax = std::max(xmax, v);
                }
            else
            {
                xmin = request.xLower;
                xmax = request.xUpper;
            }
            auto scale = [&](double v) { return request.logX && xmin > 0 ? std::log(v) : v; };
            const double low = scale(xmin), span = std::max(scale(xmax) - low, 1e-300);
            QVector<Bucket> bins(width);
            QVector<Point> exact;
            bool overflow = false;
            Point before, after;
            qint64 inside = 0;
            for (qint64 i = 0; i < x.count; ++i)
            {
                if ((i & 4095) == 0 && cancel.isCancelled())
                    return NodeResult::cancelled();
                const auto xv = x.at(b, i), yv = y.at(b, i);
                ymin = std::min(ymin, yv);
                ymax = std::max(ymax, yv);
                if (type == 2)
                    require(xv >= 0 && yv >= 0, "Invalid spectrum value");
                Point p{xv, yv, i};
                if (xv < xmin)
                {
                    if (before.index < 0 || xv > before.x)
                        before = p;
                    continue;
                }
                if (xv > xmax)
                {
                    if (after.index < 0 || xv < after.x)
                        after = p;
                    continue;
                }
                ++inside;
                if (!overflow)
                {
                    exact.push_back(p);
                    if (exact.size() > width * 4)
                    {
                        exact.clear();
                        overflow = true;
                    }
                }
                int bin =
                    std::clamp(int(std::min(double(width - 1), (scale(xv) - low) / span * width)),
                               0, width - 1);
                bins[bin].add(p);
            }
            QVector<Point> points;
            if (before.index >= 0)
                points.push_back(before);
            if (!overflow)
                points += exact;
            else
                for (const auto &bin : bins)
                    if (bin.first.index >= 0)
                        for (const auto &p : {bin.first, bin.low, bin.high, bin.last})
                            points.push_back(p);
            if (after.index >= 0)
                points.push_back(after);
            std::sort(points.begin(), points.end(),
                      [](auto a, auto c) { return a.index < c.index; });
            points.erase(std::unique(points.begin(), points.end(),
                                     [](auto a, auto c) { return a.index == c.index; }),
                         points.end());
            QVector<double> xx, yy;
            for (auto p : points)
            {
                xx.push_back(p.x);
                yy.push_back(p.y);
            }
            reduced = inside > points.size();
            bounds = QJsonArray{xmin, xmax, ymin, ymax};
            if (type == 1)
                data = makeData(SeriesData{xx, yy, xa, ya});
            else
                data = makeData(SpectrumData{xx, yy, xa, ya});
        }
        else if (type == 3)
        {
            quint32 rows, cols;
            s >> rows >> cols;
            require(rows && cols && quint64(rows) * cols <= PlotLimits::maxNumbers,
                    "Invalid grid dimensions");
            auto xa = axis(s), ya = axis(s), va = axis(s);
            auto x = array(s, b, budget), y = array(s, b, budget), v = array(s, b, budget);
            require(x.count == cols && y.count == rows && v.count == quint64(rows) * cols,
                    "Invalid grid lengths");
            auto cx = interval(b, x, request.window, request.xLower, request.xUpper),
                 ry = interval(b, y, request.window, request.yLower, request.yUpper);
            GridData g;
            g.xAxis = xa;
            g.yAxis = ya;
            g.valueAxis = va;
            g.columns = std::min<qint64>(width, cx.second - cx.first);
            g.rows = std::min<qint64>(height, ry.second - ry.first);
            // The product is independently capped, even at high export DPI.
            while (g.columns * g.rows > PlotLimits::maxHeatmapCells)
            {
                g.columns = (g.columns + 1) / 2;
                g.rows = (g.rows + 1) / 2;
            }
            g.x.resize(g.columns);
            g.y.resize(g.rows);
            g.values.resize(g.columns * g.rows);
            auto cspan = cx.second - cx.first, rspan = ry.second - ry.first;
            for (qsizetype c = 0; c < g.columns; ++c)
                g.x[c] =
                    x.at(b, cx.first + (g.columns == 1 ? 0 : c * (cspan - 1) / (g.columns - 1)));
            for (qsizetype r = 0; r < g.rows; ++r)
            {
                if (cancel.isCancelled())
                    return NodeResult::cancelled();
                g.y[r] = y.at(b, ry.first + (g.rows == 1 ? 0 : r * (rspan - 1) / (g.rows - 1)));
                if (request.gridSampleCoordinates)
                {
                    const auto sr = ry.first + (g.rows == 1 ? 0 : r * (rspan-1) / (g.rows-1));
                    for (qsizetype c = 0; c < g.columns; ++c)
                    {
                        const auto sc = cx.first + (g.columns == 1 ? 0 : c * (cspan-1) / (g.columns-1));
                        g.values[r*g.columns+c] = v.at(b, sr*cols+sc);
                    }
                    continue;
                }
                QVector<bool> assigned(g.columns, false);
                for (auto sr = ry.first + r * rspan / g.rows;
                     sr < ry.first + (r + 1) * rspan / g.rows; ++sr)
                    for (auto sc = cx.first; sc < cx.second; ++sc)
                    {
                        const auto c =
                            std::min<qint64>(g.columns - 1, (sc - cx.first) * g.columns / cspan);
                        auto value = v.at(b, sr * cols + sc);
                        auto &peak = g.values[r * g.columns + c];
                        if (!assigned[c] || std::abs(value) > std::abs(peak))
                        {
                            peak = value;
                            assigned[c] = true;
                        }
                    }
            }
            reduced = g.rows < rspan || g.columns < cspan;
            bounds = QJsonArray{x.at(b, 0), x.at(b, cols - 1), y.at(b, 0), y.at(b, rows - 1)};
            data = makeData(std::move(g));
        }
        else
        {
            quint32 cols, rows;
            s >> cols >> rows;
            require(cols && cols <= 10000 && quint64(cols) * rows <= PlotLimits::maxNumbers,
                    "Invalid table dimensions");
            TableData t;
            for (quint32 i = 0; i < cols; ++i)
                t.columns.push_back(string(s));
            const auto n =
                std::min<quint32>(rows, std::min<quint32>(PlotLimits::maxTableRows,
                                                          PlotLimits::maxTableCells / cols));
            reduced = n < rows;
            for (quint32 r = 0; r < n; ++r)
            {
                auto a = array(s, b, budget);
                require(a.count == cols, "Invalid table row");
                QVector<double> row;
                row.reserve(cols);
                for (quint32 c = 0; c < cols; ++c)
                    row.push_back(a.at(b, c));
                t.rows.push_back(row);
            }
            data = makeData(std::move(t));
        }
        require(s.status() == QDataStream::Ok, "Truncated plot metadata");
        if (type != 4)
            require(b.pos() == b.size(), "Trailing plot data");
        const auto error = validateData(data);
        if (!error.isEmpty())
            return NodeResult::failure(error);
        return NodeResult::success(
            {{"data", data}},
            {{"display_reduced", reduced}, {"bounds", bounds}, {"bytes_read", double(b.bytes)}});
    }
    catch (const std::exception &e)
    {
        return cancel.isCancelled() ? NodeResult::cancelled()
                                    : NodeResult::failure(QString::fromUtf8(e.what()));
    }
}
} // namespace aster
