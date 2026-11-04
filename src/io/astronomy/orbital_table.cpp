#include "io/astronomy/orbital_table.h"
#include "math/astronomy/orbital_elements.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QResource>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <stdexcept>

static void initializeOrbitalResources() { Q_INIT_RESOURCE(orbital_solutions); }
namespace aster::astronomy {
namespace {
constexpr qsizetype maximumBytes = 32 * 1024 * 1024;
constexpr size_t maximumRows = 500000;
QByteArray resource(const QString& name)
{
    static const bool initialized = [] { initializeOrbitalResources(); return true; }();
    (void)initialized;
    QFile file(":/orbital_solutions/" + name);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes)
        throw std::runtime_error("Orbital resource missing or oversized");
    return file.readAll();
}
const QJsonObject& catalogue()
{
    static const auto object = [] {
        const auto doc = QJsonDocument::fromJson(resource("catalogue.json"));
        if (!doc.isObject() || doc.object()["schema"].toInt() != 1)
            throw std::runtime_error("Invalid orbital catalogue");
        return doc.object()["solutions"].toObject();
    }();
    return object;
}
QString sha256(const QByteArray& bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()); }
OrbitalFormat format(const QString& value)
{
    if (value == "la2010_ecc") return OrbitalFormat::La2010;
    if (value == "zb_ecc") return OrbitalFormat::ZbEccentricity;
    if (value == "pt_zb18a") return OrbitalFormat::PrecessionTilt;
    throw std::invalid_argument("Unknown orbital table schema");
}
}
OrbitalTable OrbitalTable::parse(const QByteArray& ascii, OrbitalFormat format)
{
    if (ascii.isEmpty() || ascii.size() > maximumBytes)
        throw std::runtime_error("Orbital ASCII table is empty or exceeds 32 MiB");
    OrbitalTable table;
    const bool pt = format == OrbitalFormat::PrecessionTilt;
    if (pt) {
        table.m_channels[OrbitalQuantity::Obliquity] = {};
        table.m_channels[OrbitalQuantity::ClimaticPrecession] = {};
        table.m_channels[OrbitalQuantity::AxialPrecession] = {};
    } else table.m_channels[OrbitalQuantity::Eccentricity] = {};
    QBuffer buffer; buffer.setData(ascii); buffer.open(QIODevice::ReadOnly);
    int lineNumber = 0; bool headerSeen = false;
    const auto fail = [&] {
        throw std::runtime_error(QString("Invalid orbital table at line %1 (schema, finite values or increasing age)")
                                     .arg(lineNumber).toStdString());
    };
    while (!buffer.atEnd()) {
        auto line = buffer.readLine().trimmed(); ++lineNumber;
        if (lineNumber == 1 && line.startsWith("\xEF\xBB\xBF")) line.remove(0,3);
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (format == OrbitalFormat::ZbEccentricity && !headerSeen && table.m_ages.empty() &&
            line.simplified() == "age ecc inc") { headerSeen = true; continue; }
        const auto fields = line.simplified().split(' ');
        const int columns = pt ? 4 : format == OrbitalFormat::La2010 ? 2 : 3;
        if (fields.size() != columns || table.m_ages.size() >= maximumRows) fail();
        double v[4]{};
        for (int i=0; i<columns; ++i) {
            bool ok = false;
            v[i] = QByteArray(fields[i]).replace('D','E').replace('d','e').toDouble(&ok);
            if (!ok || !std::isfinite(v[i])) fail();
        }
        const double age = format == OrbitalFormat::ZbEccentricity ? v[0] : -v[0];
        if (age < 0 || age > 300000 || (table.m_ages.empty() ? age != 0 : age <= table.m_ages.back())) fail();
        if (pt) {
            if (age > 100000 || v[1] < 0 || v[1] > pi/2 || std::abs(v[2]) > pi+1e-10 || std::abs(v[3]) >= 1) fail();
            table.m_channels[OrbitalQuantity::Obliquity].push_back(v[1]);
            table.m_channels[OrbitalQuantity::AxialPrecession].push_back(v[2]);
            table.m_channels[OrbitalQuantity::ClimaticPrecession].push_back(v[3]);
        } else {
            if (v[1] < 0 || v[1] >= 1) fail();
            // HCI orbital inclination is checked but never exposed as axial tilt.
            if (columns == 3 && (v[2] < 0 || v[2] > 180)) fail();
            table.m_channels[OrbitalQuantity::Eccentricity].push_back(v[1]);
        }
        if (!table.m_ages.empty()) {
            const double delta = age - table.m_ages.back();
            if (table.m_ages.size() == 1) table.m_minimumSpacing = delta;
            table.m_minimumSpacing = std::min(table.m_minimumSpacing,delta);
            table.m_maximumSpacing = std::max(table.m_maximumSpacing,delta);
        }
        table.m_ages.push_back(age);
    }
    if (table.size() < 2) throw std::runtime_error("Orbital table needs at least two samples");
    return table;
}
bool OrbitalTable::supports(OrbitalQuantity quantity) const
{ return m_channels.find(quantity) != m_channels.end(); }
double OrbitalTable::at(double age, OrbitalQuantity quantity) const
{
    if (!supports(quantity)) throw std::invalid_argument("This orbital solution does not provide that quantity");
    if (!std::isfinite(age) || age < 0 || age > maximumAge())
        throw std::out_of_range("Age outside this orbital table; extrapolation is disabled");
    const auto right = std::lower_bound(m_ages.begin(),m_ages.end(),age);
    const size_t index = size_t(right-m_ages.begin());
    const auto& values = m_channels.at(quantity);
    if (*right == age) return values[index];
    const double fraction = (age-m_ages[index-1])/(m_ages[index]-m_ages[index-1]);
    double difference = values[index]-values[index-1];
    if (quantity == OrbitalQuantity::AxialPrecession) difference = std::remainder(difference,2*pi);
    const double value = std::fma(fraction,difference,values[index-1]);
    return quantity == OrbitalQuantity::AxialPrecession ? std::remainder(value,2*pi) : value;
}
QStringList eccentricitySolutionIds()
{
    QStringList ids;
    for (auto it=catalogue().begin();it!=catalogue().end();++it)
        if (it.value().toObject()["capabilities"].toArray().contains("eccentricity")) ids << it.key();
    return ids;
}
QJsonObject orbitalSolutionMetadata(const QString& id)
{
    if (!catalogue().contains(id)) throw std::invalid_argument("Unknown orbital solution ID");
    return catalogue()[id].toObject();
}
OrbitalDataset orbitalDataset(const QString& id)
{
    // Ten fixed catalogue entries: bounded lazy cache, shared immutable tables.
    // Hold the lock through construction so concurrent first requests load once.
    static std::mutex mutex;
    static std::map<QString,OrbitalDataset> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto found = cache.find(id); if (found != cache.end()) return found->second;
    auto metadata = orbitalSolutionMetadata(id);
    const auto compressed = resource(metadata["resource"].toString());
    if (compressed.size() < 5 || qFromBigEndian<quint32>(compressed.constData()) > quint32(maximumBytes))
        throw std::runtime_error("Invalid compressed orbital resource");
    const auto bytes = qUncompress(compressed);
    if (bytes.size() != metadata["source_bytes"].toInt() || sha256(bytes) != metadata["source_sha256"].toString())
        throw std::runtime_error("Orbital data checksum or size mismatch");
    auto table = std::make_shared<OrbitalTable>(OrbitalTable::parse(bytes,format(metadata["format"].toString())));
    if (table->size() != size_t(metadata["rows"].toInt()) || table->maximumAge() != metadata["maximum_age_ka"].toDouble())
        throw std::runtime_error("Orbital table coverage/count does not match its catalogue");
    OrbitalDataset value{std::move(table),std::move(metadata)};
    cache.emplace(id,value); return value;
}
OrbitalDataset precessionTiltDataset(const QString& path)
{
    if (path.trimmed().isEmpty()) return orbitalDataset("pt_zb18a");
    const QFileInfo info(path);
    static const QRegularExpression name("^PT\\.De([01]\\.[0-9]{4})Td([01]\\.[0-9]{4})\\.dat$");
    const auto match = name.match(info.fileName());
    if (!match.hasMatch()) throw std::invalid_argument("Keep the official PT.DeX.XXXXTdY.YYYY.dat filename");
    const double ed = match.captured(1).toDouble(),td = match.captured(2).toDouble();
    if (ed < .995 || ed > 1.005 || td < 0 || td > 1.2 ||
        std::abs(ed*1000-std::round(ed*1000)) > 1e-8 || std::abs(td*10-std::round(td*10)) > 1e-8)
        throw std::invalid_argument("External PT file is outside the published Ed/Td grid");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumBytes)
        throw std::runtime_error("Cannot read PT table, or it exceeds 32 MiB");
    const auto bytes = file.read(maximumBytes+1);
    if (file.error() != QFileDevice::NoError) throw std::runtime_error("Failed while reading PT table");
    auto table = std::make_shared<OrbitalTable>(OrbitalTable::parse(bytes,OrbitalFormat::PrecessionTilt));
    if (table->maximumAge() != 100000 || table->size() < 100000 || table->maximumSpacing() > 2)
        throw std::runtime_error("PT table must contain the complete 0..100000 ka published solution");
    auto metadata = orbitalSolutionMetadata("pt_zb18a");
    metadata.remove("resource"); metadata["id"] = "pt_zb18a_external";
    metadata.remove("retrieved"); // The user's original download date is unknown.
    metadata["label"] = QString("PT-ZB18a (Ed=%1, Td=%2)").arg(ed,0,'f',4).arg(td,0,'f',4);
    metadata["storage"] = "external ASCII file";
    metadata["filename"] = info.fileName(); metadata["local_path"] = info.absoluteFilePath();
    metadata["source_url"] = metadata["data_directory_url"].toString()+info.fileName();
    metadata["source_sha256"] = sha256(bytes); metadata["source_bytes"] = bytes.size();
    metadata["rows"] = double(table->size()); metadata["ed"] = ed; metadata["td"] = td;
    metadata["verification"] = "schema_and_range_checked_local_file_not_authenticated_against_remote_hash";
    return {std::move(table),std::move(metadata)};
}
}
