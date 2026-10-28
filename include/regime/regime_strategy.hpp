#ifndef OCAS_REGIME_REGIME_STRATEGY_HPP
#define OCAS_REGIME_REGIME_STRATEGY_HPP

#include "core/types.hpp"

#include <memory>

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

struct RegimeParameters {
    OrbitalRegime regime;
    ManeuverPlane plane;
    double screening_radius_m;
    double min_separation_m;
    double sigma_multiplier;
    double planning_margin_s;
    double preferred_lead_orbits;
    double immediate_window_s;
    double priority_window_s;
    double max_dv_mps;
};

class OrbitalRegimeStrategy {
public:
    virtual ~OrbitalRegimeStrategy() = default;
    OrbitalRegimeStrategy(const OrbitalRegimeStrategy&) = delete;
    OrbitalRegimeStrategy& operator=(const OrbitalRegimeStrategy&) = delete;

    [[nodiscard]] virtual double calculate_regime_risk(const ConjunctionTelemetry& t) const = 0;
    [[nodiscard]] virtual EvasionDirective formulate_evasion_directive(const ConjunctionTelemetry& t) const = 0;

    [[nodiscard]] OrbitalRegime regime() const noexcept { return p_.regime; }
    [[nodiscard]] double mean_motion_rad_s() const noexcept { return n_; }
    [[nodiscard]] double orbital_period_s() const noexcept;

protected:
    OrbitalRegimeStrategy(double altitude_km, const RegimeParameters& p) noexcept;

    struct Timing {
        double execute_at_s;
        double lead_s;
        bool margin_violated;
    };

    [[nodiscard]] double proximity(double miss_m) const noexcept;
    [[nodiscard]] double time_pressure(double tca_s) const noexcept;
    [[nodiscard]] Timing plan_timing(double tca_s) const noexcept;
    [[nodiscard]] EvasionDirective complete_directive(const ConjunctionTelemetry& t, const Timing& timing,
                                                      VectorRTN direction, double gain_s) const noexcept;

private:
    RegimeParameters p_;
    double n_;
};

class LEORegime final : public OrbitalRegimeStrategy {
public:
    explicit LEORegime(double altitude_km) noexcept;
    [[nodiscard]] double calculate_regime_risk(const ConjunctionTelemetry& t) const override;
    [[nodiscard]] EvasionDirective formulate_evasion_directive(const ConjunctionTelemetry& t) const override;
};

class MEORegime final : public OrbitalRegimeStrategy {
public:
    explicit MEORegime(double altitude_km) noexcept;
    [[nodiscard]] double calculate_regime_risk(const ConjunctionTelemetry& t) const override;
    [[nodiscard]] EvasionDirective formulate_evasion_directive(const ConjunctionTelemetry& t) const override;
};

class GEORegime final : public OrbitalRegimeStrategy {
public:
    explicit GEORegime(double altitude_km) noexcept;
    [[nodiscard]] double calculate_regime_risk(const ConjunctionTelemetry& t) const override;
    [[nodiscard]] EvasionDirective formulate_evasion_directive(const ConjunctionTelemetry& t) const override;
};

class RegimeFactory {
public:
    // False for non-finite or sub-orbital altitudes. Above the GEO band maps to GEO.
    static bool classify(double altitude_km, OrbitalRegime& out) noexcept;
    static std::unique_ptr<OrbitalRegimeStrategy> create_regime(double altitude_km);
};

}  // namespace ocas

#endif
