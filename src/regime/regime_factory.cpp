// OCAS Module 4 implementation: regime strategies and factory.
#include "regime/regime_strategy.hpp"

#include <algorithm>
#include <cmath>

namespace ocas {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHour = 3600.0;
constexpr double kDay = 86400.0;
constexpr double kMinGainS = 1.0;  ///< Guards dv = disp / G against G -> 0

//                       regime              plane                      screen  min_sep  k    margin      lead  immediate    priority     dv_cap
constexpr RegimeParameters kLeo{OrbitalRegime::LEO, ManeuverPlane::AlongTrack, 1000.0, 1000.0, 2.0, 0.5 * kHour, 8.0,  6.0 * kHour,  3.0 * kDay, 1.0};
constexpr RegimeParameters kMeo{OrbitalRegime::MEO, ManeuverPlane::Radial,     3000.0, 3000.0, 2.0, 1.0 * kHour, 0.5, 12.0 * kHour,  4.0 * kDay, 0.5};
constexpr RegimeParameters kGeo{OrbitalRegime::GEO, ManeuverPlane::CrossTrack, 5000.0, 5000.0, 2.0, 2.0 * kHour, 0.25, 1.0 * kDay,   7.0 * kDay, 0.5};

double mean_motion(double altitude_km) noexcept {
    const double a = kEarthRadiusKm + altitude_km;
    return std::sqrt(kEarthMuKm3s2 / (a * a * a));
}

}  // namespace

// ---------------------------------------------------------------------------
// OrbitalRegimeStrategy (shared machinery)
// ---------------------------------------------------------------------------
OrbitalRegimeStrategy::OrbitalRegimeStrategy(double altitude_km, const RegimeParameters& p) noexcept
    : p_(p), altitude_km_(altitude_km), n_(mean_motion(altitude_km)) {}

double OrbitalRegimeStrategy::orbital_period_s() const noexcept { return 2.0 * kPi / n_; }

double OrbitalRegimeStrategy::proximity(double miss_m) const noexcept {
    return std::exp(-std::max(0.0, miss_m) / p_.screening_radius_m);
}

double OrbitalRegimeStrategy::time_pressure(double tca_s) const noexcept {
    return std::exp(-std::max(0.0, tca_s) / p_.priority_window_s);
}

OrbitalRegimeStrategy::Timing OrbitalRegimeStrategy::plan_timing(double tca_s) const noexcept {
    const double t = std::max(0.0, tca_s);
    const double preferred = p_.preferred_lead_orbits * orbital_period_s();
    if (t >= p_.planning_margin_s + preferred) {
        return {t - preferred, preferred, false};  // burn later, at the preferred lead
    }
    if (t > p_.planning_margin_s) {
        return {p_.planning_margin_s, t - p_.planning_margin_s, false};  // as early as the FCU allows
    }
    return {0.0, t, true};  // inside the planning margin: execute now
}

EvasionDirective OrbitalRegimeStrategy::complete_directive(const ConjunctionTelemetry& t, const Timing& timing,
                                                           VectorRTN direction, double gain_s) const noexcept {
    const double d = std::max(0.0, t.miss_distance_m);
    // An unusable covariance contributes nothing; the regime floor and the
    // 1.5x-miss term still size the maneuver.
    const double sigma_axis = covariance_usable(t.cov_trace_m2) ? std::sqrt(t.cov_trace_m2 / 3.0) : 0.0;

    // Aim for the largest of: regime floor, k-sigma, and 1.5x the current miss.
    // The 1.5x term guarantees a non-zero maneuver whenever the classifier acts.
    const double target = std::max({p_.min_separation_m, p_.sigma_multiplier * sigma_axis, 1.5 * d});
    const double displacement = std::sqrt(target * target - d * d);
    const double gain = std::max(gain_s, kMinGainS);

    EvasionDirective out{};
    out.alert_id = t.alert_id;
    out.regime = p_.regime;
    out.plane = p_.plane;
    out.direction = direction;
    out.delta_v_mps = displacement / gain;
    out.execute_no_later_than_s = timing.execute_at_s;
    out.lead_time_s = timing.lead_s;
    out.target_separation_m = target;
    const double achieved = out.delta_v_mps * gain;
    out.predicted_separation_m = std::sqrt(d * d + achieved * achieved);
    out.within_regime_dv_cap = out.delta_v_mps <= p_.max_dv_mps;

    if (timing.margin_violated || t.time_to_tca_sec <= p_.immediate_window_s) {
        out.urgency = Urgency::Immediate;
    } else if (t.time_to_tca_sec <= p_.priority_window_s) {
        out.urgency = Urgency::Priority;
    } else {
        out.urgency = Urgency::Routine;
    }
    return out;
}

// ---------------------------------------------------------------------------
// LEO: high speed, drag + J2 dominated, along-track phasing
// ---------------------------------------------------------------------------
LEORegime::LEORegime(double altitude_km) noexcept : OrbitalRegimeStrategy(altitude_km, kLeo) {}

double LEORegime::calculate_regime_risk(const ConjunctionTelemetry& t) const {
    // Kinetic-severity term: hypervelocity impacts fragment more completely.
    const double severity = 0.75 + 0.25 * std::min(std::max(0.0, t.relative_speed_km_s) / 15.0, 1.0);
    return proximity(t.miss_distance_m) * (0.5 + 0.5 * time_pressure(t.time_to_tca_sec)) * severity;
}

EvasionDirective LEORegime::formulate_evasion_directive(const ConjunctionTelemetry& t) const {
    const Timing timing = plan_timing(t.time_to_tca_sec);
    const double n = mean_motion_rad_s();
    const double L = timing.lead_s;
    const double gain = std::fabs(4.0 * std::sin(n * L) / n - 3.0 * L);
    return complete_directive(t, timing, VectorRTN{0.0, 1.0, 0.0}, gain);
}

// ---------------------------------------------------------------------------
// MEO: navigation constellations, radial displacement
// ---------------------------------------------------------------------------
MEORegime::MEORegime(double altitude_km) noexcept : OrbitalRegimeStrategy(altitude_km, kMeo) {}

double MEORegime::calculate_regime_risk(const ConjunctionTelemetry& t) const {
    return proximity(t.miss_distance_m) * (0.5 + 0.5 * time_pressure(t.time_to_tca_sec));
}

EvasionDirective MEORegime::formulate_evasion_directive(const ConjunctionTelemetry& t) const {
    const Timing timing = plan_timing(t.time_to_tca_sec);
    const double n = mean_motion_rad_s();
    const double s = std::sin(n * timing.lead_s);
    const double c = std::cos(n * timing.lead_s);
    const double gain = std::sqrt(s * s + 4.0 * (1.0 - c) * (1.0 - c)) / n;
    return complete_directive(t, timing, VectorRTN{1.0, 0.0, 0.0}, gain);
}

// ---------------------------------------------------------------------------
// GEO: slow encounters, long windows, cross-track inclination adjustment
// ---------------------------------------------------------------------------
GEORegime::GEORegime(double altitude_km) noexcept : OrbitalRegimeStrategy(altitude_km, kGeo) {}

double GEORegime::calculate_regime_risk(const ConjunctionTelemetry& t) const {
    // Dwell term: slow encounters spend longer inside the screening volume.
    const double dwell = 0.75 + 0.25 * std::exp(-std::max(0.0, t.relative_speed_km_s) / 1.0);
    return proximity(t.miss_distance_m) * (0.5 + 0.5 * time_pressure(t.time_to_tca_sec)) * dwell;
}

EvasionDirective GEORegime::formulate_evasion_directive(const ConjunctionTelemetry& t) const {
    const Timing timing = plan_timing(t.time_to_tca_sec);
    const double n = mean_motion_rad_s();
    const double gain = std::fabs(std::sin(n * timing.lead_s)) / n;
    return complete_directive(t, timing, VectorRTN{0.0, 0.0, 1.0}, gain);
}

// ---------------------------------------------------------------------------
// RegimeFactory
// ---------------------------------------------------------------------------
bool RegimeFactory::classify(double altitude_km, OrbitalRegime& out) noexcept {
    if (!std::isfinite(altitude_km) || altitude_km < kMinOrbitalAltitudeKm) {
        return false;
    }
    if (altitude_km < kLeoUpperKm) {
        out = OrbitalRegime::LEO;
    } else if (altitude_km < kGeoAltitudeKm - kGeoBandKm) {
        out = OrbitalRegime::MEO;
    } else {
        out = OrbitalRegime::GEO;
    }
    return true;
}

std::unique_ptr<OrbitalRegimeStrategy> RegimeFactory::create_regime(double altitude_km) {
    OrbitalRegime r{};
    if (!classify(altitude_km, r)) {
        return nullptr;
    }
    switch (r) {
        case OrbitalRegime::LEO: return std::make_unique<LEORegime>(altitude_km);
        case OrbitalRegime::MEO: return std::make_unique<MEORegime>(altitude_km);
        case OrbitalRegime::GEO: return std::make_unique<GEORegime>(altitude_km);
    }
    return nullptr;
}

}  // namespace ocas
