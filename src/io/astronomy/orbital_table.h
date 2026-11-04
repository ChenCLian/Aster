#ifndef ASTER_IO_ASTRONOMY_ORBITAL_TABLE_H
#define ASTER_IO_ASTRONOMY_ORBITAL_TABLE_H
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <map>
#include <memory>
#include <vector>

namespace aster::astronomy {
// Scalar channels deliberately cannot be converted to OrbitalElements.
// In particular inclination/axial precession are not obliquity/perihelion.
enum class OrbitalQuantity { Eccentricity, Obliquity, ClimaticPrecession, AxialPrecession };
enum class OrbitalFormat { La2010, ZbEccentricity, PrecessionTilt };
class OrbitalTable {
public:
    static OrbitalTable parse(const QByteArray& ascii, OrbitalFormat format);
    bool supports(OrbitalQuantity quantity) const;
    double at(double ageKaBeforeJ2000, OrbitalQuantity quantity) const;
    double maximumAge() const { return m_ages.back(); }
    double minimumSpacing() const { return m_minimumSpacing; }
    double maximumSpacing() const { return m_maximumSpacing; }
    size_t size() const { return m_ages.size(); }
private:
    std::vector<double> m_ages;
    std::map<OrbitalQuantity, std::vector<double>> m_channels;
    double m_minimumSpacing = 0, m_maximumSpacing = 0;
};
struct OrbitalDataset {
    std::shared_ptr<const OrbitalTable> table;
    QJsonObject provenance;
};
// Registry/GUI may read the small catalogue. Numerical tables load only on demand.
QStringList eccentricitySolutionIds();
QJsonObject orbitalSolutionMetadata(const QString& id);
OrbitalDataset orbitalDataset(const QString& id);
// Empty path chooses the embedded Ed=1,Td=1 table. External tables must retain
// NOAA's PT.De....Td....dat filename and four-column, negative-time schema.
OrbitalDataset precessionTiltDataset(const QString& path = {});
}
#endif
