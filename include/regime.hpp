#ifndef OCAS_REGIME_HPP
#define OCAS_REGIME_HPP

#include "types.hpp"

namespace ocas {

// Burn sizing uses the Clohessy-Wiltshire solution (circular orbit; no J2, drag or
// third body) for an impulse dv applied a lead time L before TCA:
//   LEO along-track: G = |4 sin(nL)/n - 3L|
//   MEO radial:      G = sqrt(sin^2(nL) + 4(1 - cos nL)^2) / n
//   GEO cross-track: G = |sin(nL)| / n
// The schema has no RTN miss components, so the offset is assumed orthogonal to the
// miss vector (the conservative choice): disp = sqrt(target^2 - d^2), dv = disp / G.
// Burn sign is fixed positive; the FCU must confirm it against its own ephemeris.

inline constexpr double kEarthMuKm3s2 = 398600.4418;
inline constexpr double kEarthRadiusKm = 6378.137;
inline constexpr double kLeoUpperKm = 2000.0;
inline constexpr double kGeoAltitudeKm = 35786.0;
inline constexpr double kGeoBandKm = 200.0;  // IADC GEO protected region half-width
inline constexpr double kMinOrbitalAltitudeKm = 100.0;

struct Regime {
    OrbitalRegime kind;
    ManeuverPlane plane;
    double screening_radius_m;
    double min_separation_m;
    double sigma_multiplier;
    double planning_margin_s;
    double preferred_lead_orbits;
    double immediate_window_s;
    double priority_window_s;
    double max_dv_mps;
    double altitude_km = 0.0;
    double n = 0.0;  // mean motion [rad/s]

    [[nodiscard]] double period_s() const noexcept;
};

// False for non-finite or sub-orbital altitudes. Above the GEO band maps to GEO.
bool classify_regime(double altitude_km, OrbitalRegime& out) noexcept;
bool make_regime(double altitude_km, Regime& out) noexcept;

// Regime-specific criticality in [0, 1].
double regime_risk(const Regime& r, const ConjunctionTelemetry& t) noexcept;
EvasionDirective plan_evasion(const Regime& r, const ConjunctionTelemetry& t) noexcept;

}  // namespace ocas

#endif
