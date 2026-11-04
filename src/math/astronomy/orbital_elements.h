#ifndef ASTER_MATH_ASTRONOMY_ORBITAL_ELEMENTS_H
#define ASTER_MATH_ASTRONOMY_ORBITAL_ELEMENTS_H
namespace aster::astronomy {
inline constexpr double pi = 3.1415926535897932384626433832795;
inline constexpr double radians = pi / 180.0;
// All internal angles are radians. perihelion is HELIOCENTRIC, measured
// from the moving equinox (La2004 pibar), not the solar longitude of perihelion.
struct OrbitalElements {
    double eccentricity = 0;
    double obliquity = 0;
    double perihelion = 0;
};
}
#endif
