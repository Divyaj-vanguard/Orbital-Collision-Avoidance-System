#include "regime.hpp"

#include <algorithm>
#include <cmath>

namespace ocas {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHour = 3600.0;
constexpr double kDay = 86400.0;
constexpr double kMinGainS = 1.0;  // guards dv = disp / G against G -> 0

//                 kind                plane                      screen  min_sep  k    margin      lead  immediate    priority     dv_cap
constexpr Regime kLeo{OrbitalRegime::LEO, ManeuverPlane::AlongTrack, 1000.0, 1000.0, 2.0, 0.5 * kHour, 8.0,  6.0 * kHour,  3.0 * kDay, 1.0};
constexpr Regime kMeo{OrbitalRegime::MEO, ManeuverPlane::Radial,     3000.0, 3000.0, 2.0, 1.0 * kHour, 0.5, 12.0 * kHour,  4.0 * kDay, 0.5};
constexpr Regime kGeo{OrbitalRegime::GEO, ManeuverPlane::CrossTrack, 5000.0, 5000.0, 2.0, 2.0 * kHour, 0.25, 1.0 * kDay,   7.0 * kDay, 0.5};

}  // namespace

double Regime::period_s() const noexcept { return 2.0 * kPi / n; }

bool classify_regime(double altitude_km, OrbitalRegime& out) noexcept {
    if (!std::isfinite(altitude_km) || altitude_km < kMinOrbitalAltitudeKm) return false;
    if (altitude_km < kLeoUpperKm) out = OrbitalRegime::LEO;
    else if (altitude_km < kGeoAltitudeKm - kGeoBandKm) out = OrbitalRegime::MEO;
    else out = OrbitalRegime::GEO;
    return true;
}

bool make_regime(double altitude_km, Regime& out) noexcept {
    OrbitalRegime kind{};
    if (!classify_regime(altitude_km, kind)) return false;
    out = kind == OrbitalRegime::LEO ? kLeo : kind == OrbitalRegime::MEO ? kMeo : kGeo;
    out.altitude_km = altitude_km;
    const double a = kEarthRadiusKm + altitude_km;
    out.n = std::sqrt(kEarthMuKm3s2 / (a * a * a));
    return true;
}

double regime_risk(const Regime& r, const ConjunctionTelemetry& t) noexcept {
    const double proximity = std::exp(-std::max(0.0, t.miss_distance_m) / r.screening_radius_m);
    const double time_pressure = std::exp(-std::max(0.0, t.time_to_tca_sec) / r.priority_window_s);
    const double v = std::max(0.0, t.relative_speed_km_s);
    double factor = 1.0;
    if (r.kind == OrbitalRegime::LEO) factor = 0.75 + 0.25 * std::min(v / 15.0, 1.0);  // hypervelocity fragments more
    if (r.kind == OrbitalRegime::GEO) factor = 0.75 + 0.25 * std::exp(-v / 1.0);         // slow passes dwell longer
    return proximity * (0.5 + 0.5 * time_pressure) * factor;
}

EvasionDirective plan_evasion(const Regime& r, const ConjunctionTelemetry& t) noexcept {
    // Burn at the preferred lead if there is time, else as early as the FCU allows, else now.
    const double tca = std::max(0.0, t.time_to_tca_sec);
    const double preferred = r.preferred_lead_orbits * r.period_s();
    double execute_at = 0.0, lead = tca;
    bool margin_violated = false;
    if (tca >= r.planning_margin_s + preferred) {
        execute_at = tca - preferred;
        lead = preferred;
    } else if (tca > r.planning_margin_s) {
        execute_at = r.planning_margin_s;
        lead = tca - r.planning_margin_s;
    } else {
        margin_violated = true;
    }

    const double nl = r.n * lead;
    double gain = 0.0;
    VectorRTN dir{};
    switch (r.kind) {
        case OrbitalRegime::LEO:
            gain = std::fabs(4.0 * std::sin(nl) / r.n - 3.0 * lead);
            dir.t = 1.0;
            break;
        case OrbitalRegime::MEO:
            gain = std::sqrt(std::sin(nl) * std::sin(nl) + 4.0 * (1.0 - std::cos(nl)) * (1.0 - std::cos(nl))) / r.n;
            dir.r = 1.0;
            break;
        case OrbitalRegime::GEO:
            gain = std::fabs(std::sin(nl)) / r.n;
            dir.n = 1.0;
            break;
    }
    gain = std::max(gain, kMinGainS);

    const double d = std::max(0.0, t.miss_distance_m);
    const double sigma_axis = covariance_usable(t.cov_trace_m2) ? std::sqrt(t.cov_trace_m2 / 3.0) : 0.0;
    // The 1.5x-miss term guarantees a non-zero manoeuvre whenever the classifier acts.
    const double target = std::max({r.min_separation_m, r.sigma_multiplier * sigma_axis, 1.5 * d});

    EvasionDirective out{};
    out.alert_id = t.alert_id;
    out.regime = r.kind;
    out.plane = r.plane;
    out.direction = dir;
    out.delta_v_mps = std::sqrt(target * target - d * d) / gain;
    out.execute_no_later_than_s = execute_at;
    out.lead_time_s = lead;
    out.target_separation_m = target;
    const double achieved = out.delta_v_mps * gain;
    out.predicted_separation_m = std::sqrt(d * d + achieved * achieved);
    out.within_regime_dv_cap = out.delta_v_mps <= r.max_dv_mps;
    if (margin_violated || t.time_to_tca_sec <= r.immediate_window_s) out.urgency = Urgency::Immediate;
    else if (t.time_to_tca_sec <= r.priority_window_s) out.urgency = Urgency::Priority;
    else out.urgency = Urgency::Routine;
    return out;
}

}  // namespace ocas
