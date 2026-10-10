// ============================================================================
// OCAS Module 4: Orbital Regime Strategy & Evasion Advisory Engine
// ============================================================================
// Strategy + Factory: RegimeFactory::create_regime(altitude) returns a
// LEORegime, MEORegime or GEORegime behind the OrbitalRegimeStrategy vtable.
//
// Maneuver sizing uses the Clohessy-Wiltshire (CW) solution for an impulse
// dv applied a lead time L before TCA, circular reference orbit, mean motion n.
// Displacement at TCA = dv * G(L), with gain G per maneuver plane:
//   LEO  along-track (T):  G = | 4 sin(nL)/n - 3L |                 (secular drift)
//   MEO  radial      (R):  G = sqrt( sin^2(nL) + 4(1 - cos nL)^2 ) / n
//   GEO  cross-track (N):  G = | sin(nL) | / n
// Required displacement assumes the maneuver offset is orthogonal to the
// unknown miss vector (the 6-field schema carries no RTN miss components):
//   new_miss = sqrt(d^2 + disp^2)  =>  disp = sqrt(target^2 - d^2)
// which is the conservative (larger) choice versus a collinear offset.
//
// Modelling limits (stated, not hidden): CW assumes a circular orbit and
// ignores J2, drag and third-body forces. Regime risk weights are
// engineering heuristics, not trained values. Burn direction sign is fixed
// positive because the miss geometry is not in the schema; the OEM FCU must
// confirm sign against its own ephemeris.
// ============================================================================
#ifndef OCAS_REGIME_REGIME_STRATEGY_HPP
#define OCAS_REGIME_REGIME_STRATEGY_HPP

#include "core/types.hpp"

#include <memory>

namespace ocas {

inline constexpr double kEarthMuKm3s2 = 398600.4418;   ///< GM_Earth [km^3/s^2]
inline constexpr double kEarthRadiusKm = 6378.137;     ///< WGS-84 equatorial radius [km]
inline constexpr double kLeoUpperKm = 2000.0;
inline constexpr double kGeoAltitudeKm = 35786.0;
inline constexpr double kGeoBandKm = 200.0;            ///< IADC GEO protected region half-width
inline constexpr double kMinOrbitalAltitudeKm = 100.0;

struct RegimeParameters {
    OrbitalRegime regime;
    ManeuverPlane plane;
    double screening_radius_m;     ///< e-folding distance of the proximity term
    double min_separation_m;       ///< Floor on the advisory target separation
    double sigma_multiplier;       ///< Target >= k * sigma_axis
    double planning_margin_s;      ///< Earliest burn: FCU planning/upload time
    double preferred_lead_orbits;  ///< Preferred burn-to-TCA lead, in orbital periods
    double immediate_window_s;     ///< t_TCA below this => IMMEDIATE
    double priority_window_s;      ///< t_TCA below this => PRIORITY; also time-pressure scale
    double max_dv_mps;             ///< Per-advisory delta-v cap before escalation
};

class OrbitalRegimeStrategy {
public:
    virtual ~OrbitalRegimeStrategy() = default;
    OrbitalRegimeStrategy(const OrbitalRegimeStrategy&) = delete;
    OrbitalRegimeStrategy& operator=(const OrbitalRegimeStrategy&) = delete;

    /// Regime-specific criticality in [0, 1].
    [[nodiscard]] virtual double calculate_regime_risk(const ConjunctionTelemetry& t) const = 0;
    /// Platform-agnostic evasion advisory for this regime.
    [[nodiscard]] virtual EvasionDirective formulate_evasion_directive(const ConjunctionTelemetry& t) const = 0;

    [[nodiscard]] OrbitalRegime regime() const noexcept { return p_.regime; }
    [[nodiscard]] const RegimeParameters& params() const noexcept { return p_; }
    [[nodiscard]] double altitude_km() const noexcept { return altitude_km_; }
    [[nodiscard]] double mean_motion_rad_s() const noexcept { return n_; }
    [[nodiscard]] double orbital_period_s() const noexcept;

protected:
    OrbitalRegimeStrategy(double altitude_km, const RegimeParameters& p) noexcept;

    struct Timing {
        double execute_at_s;  ///< Seconds after CDM epoch
        double lead_s;        ///< Burn-to-TCA interval
        bool margin_violated; ///< t_TCA too short to honour the planning margin
    };

    [[nodiscard]] double proximity(double miss_m) const noexcept;
    [[nodiscard]] double time_pressure(double tca_s) const noexcept;
    [[nodiscard]] Timing plan_timing(double tca_s) const noexcept;

    /// Shared tail of every formulate_evasion_directive(): sizes dv from the
    /// plane-specific CW gain and fills the remaining fields.
    [[nodiscard]] EvasionDirective complete_directive(const ConjunctionTelemetry& t, const Timing& timing,
                                                      VectorRTN direction, double gain_s) const noexcept;

private:
    RegimeParameters p_;
    double altitude_km_;
    double n_;  ///< Mean motion [rad/s]
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
    /// Classify an altitude. Returns false for non-finite or sub-orbital input.
    /// Above the GEO band (super-synchronous) maps to GEO: low relative speeds
    /// and long decision windows match that strategy best.
    static bool classify(double altitude_km, OrbitalRegime& out) noexcept;

    /// Init-time only (heap allocates). Returns nullptr for invalid altitudes.
    static std::unique_ptr<OrbitalRegimeStrategy> create_regime(double altitude_km);
};

}  // namespace ocas

#endif  // OCAS_REGIME_REGIME_STRATEGY_HPP
