/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Polymorphic Propulsion Implementation
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Implements strict OOP principles:
 *  - Abstract class ThrusterEngine hardware contract
 *  - Concrete derived classes: ChemicalThruster, IonThruster, ColdGasThruster
 *  - ThrusterDispatcher composition with automated fallback
 *  - Exact Tsiolkovsky rocket equation calculations
 * ============================================================================
 */

#include "thruster_engine.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace ocas {

namespace {

BurnSolution compute_tsiolkovsky_burn(const std::string& name,
                                      double thrust_n,
                                      double isp_s,
                                      double min_pulse_s,
                                      double fuel_reserve_kg,
                                      bool operational,
                                      double delta_v,
                                      double mass) {
    BurnSolution sol;
    sol.engine_name = name;
    sol.delta_v_ms = delta_v;

    if (!operational) {
        sol.feasible = false;
        sol.diagnostics = "Engine offline or health flag faulted.";
        return sol;
    }

    if (delta_v <= 0.0 || mass <= 0.0 || thrust_n <= 0.0 || isp_s <= 0.0) {
        sol.feasible = false;
        sol.diagnostics = "Invalid physics parameters (delta_v or mass <= 0).";
        return sol;
    }

    const double ve = isp_s * G0_STANDARD;
    // Tsiolkovsky: dm = m0 * (1 - e^(-dV / ve))
    const double dm = mass * (1.0 - std::exp(-delta_v / ve));
    const double mdot = thrust_n / ve;
    const double tb = dm / mdot;

    sol.propellant_mass_kg = dm;
    sol.burn_duration_sec = tb;
    sol.mass_flow_rate_kgs = mdot;

    if (dm > fuel_reserve_kg) {
        sol.feasible = false;
        std::ostringstream oss;
        oss << "INSUFFICIENT_PROPELLANT: Required " << dm << " kg > available "
            << fuel_reserve_kg << " kg.";
        sol.diagnostics = oss.str();
        return sol;
    }

    if (tb < min_pulse_s) {
        sol.feasible = false;
        std::ostringstream oss;
        oss << "PULSE_DURATION_TOO_SHORT: Computed burn " << tb << " s < min pulse "
            << min_pulse_s << " s.";
        sol.diagnostics = oss.str();
        return sol;
    }

    sol.feasible = true;
    sol.diagnostics = "Nominal burn solution computed successfully.";
    return sol;
}

bool execute_tsiolkovsky_fire(double thrust_n,
                              double isp_s,
                              double burn_duration,
                              double& engine_fuel_kg,
                              double& spacecraft_fuel_reserve,
                              bool operational) {
    if (!operational) {
        return false;
    }

    if (burn_duration <= 0.0 || engine_fuel_kg <= 0.0 || spacecraft_fuel_reserve <= 0.0) {
        return false;
    }

    const double ve = isp_s * G0_STANDARD;
    const double mdot = thrust_n / ve;
    const double fuel_spent = mdot * burn_duration;

    if (fuel_spent > engine_fuel_kg || fuel_spent > spacecraft_fuel_reserve) {
        return false; // Fuel starvation abort
    }

    engine_fuel_kg -= fuel_spent;
    spacecraft_fuel_reserve -= fuel_spent;
    return true;
}

} // anonymous namespace

/* ============================================================================
 * ChemicalThruster Implementation (400 N, 310 s)
 * ============================================================================ */

bool ChemicalThruster::is_feasible(double required_delta_v,
                                  double time_to_closest_approach,
                                  double spacecraft_mass) const {
    if (!operational_status) return false;
    if (required_delta_v <= 0.0 || spacecraft_mass <= 0.0 || time_to_closest_approach <= 0.0) {
        return false;
    }

    BurnSolution sol = calculate_burn(required_delta_v, spacecraft_mass);
    if (!sol.feasible) return false;

    // Must complete burn before encounter
    if (sol.burn_duration_sec >= time_to_closest_approach) {
        return false;
    }

    return true;
}

BurnSolution ChemicalThruster::calculate_burn(double required_delta_v,
                                             double spacecraft_mass) const {
    return compute_tsiolkovsky_burn(engine_name, thrust_newtons, specific_impulse_sec,
                                   min_pulse_time_sec, fuel_mass_kg, operational_status,
                                   required_delta_v, spacecraft_mass);
}

bool ChemicalThruster::execute_burn(double burn_duration,
                                   double &spacecraft_fuel_reserve) {
    return execute_tsiolkovsky_fire(thrust_newtons, specific_impulse_sec, burn_duration,
                                   fuel_mass_kg, spacecraft_fuel_reserve, operational_status);
}

/* ============================================================================
 * IonThruster Implementation (0.1 N, 3200 s)
 * ============================================================================ */

bool IonThruster::is_feasible(double required_delta_v,
                             double time_to_closest_approach,
                             double spacecraft_mass) const {
    if (!operational_status) return false;
    if (required_delta_v <= 0.0 || spacecraft_mass <= 0.0 || time_to_closest_approach <= 0.0) {
        return false;
    }

    BurnSolution sol = calculate_burn(required_delta_v, spacecraft_mass);
    if (!sol.feasible) return false;

    // Critical Aerospace Rule: strictly rejects maneuver if burn duration > 0.8 * t_TCA
    // Ion thrusters have low thrust (0.1 N) and cannot execute short-fused conjunction burns
    if (sol.burn_duration_sec > 0.8 * time_to_closest_approach) {
        return false;
    }

    return true;
}

BurnSolution IonThruster::calculate_burn(double required_delta_v,
                                        double spacecraft_mass) const {
    return compute_tsiolkovsky_burn(engine_name, thrust_newtons, specific_impulse_sec,
                                   min_pulse_time_sec, fuel_mass_kg, operational_status,
                                   required_delta_v, spacecraft_mass);
}

bool IonThruster::execute_burn(double burn_duration,
                              double &spacecraft_fuel_reserve) {
    return execute_tsiolkovsky_fire(thrust_newtons, specific_impulse_sec, burn_duration,
                                   fuel_mass_kg, spacecraft_fuel_reserve, operational_status);
}

/* ============================================================================
 * ColdGasThruster Implementation (10 N, 70 s)
 * ============================================================================ */

bool ColdGasThruster::is_feasible(double required_delta_v,
                                 double time_to_closest_approach,
                                 double spacecraft_mass) const {
    if (!operational_status) return false;
    if (required_delta_v <= 0.0 || spacecraft_mass <= 0.0 || time_to_closest_approach <= 0.0) {
        return false;
    }

    // Strict constraint: Cold Gas RCS is reserved for micro-adjustments (Delta-V < 0.05 m/s)
    if (required_delta_v >= 0.05) {
        return false;
    }

    BurnSolution sol = calculate_burn(required_delta_v, spacecraft_mass);
    if (!sol.feasible) return false;

    if (sol.burn_duration_sec >= time_to_closest_approach) {
        return false;
    }

    return true;
}

BurnSolution ColdGasThruster::calculate_burn(double required_delta_v,
                                            double spacecraft_mass) const {
    return compute_tsiolkovsky_burn(engine_name, thrust_newtons, specific_impulse_sec,
                                   min_pulse_time_sec, fuel_mass_kg, operational_status,
                                   required_delta_v, spacecraft_mass);
}

bool ColdGasThruster::execute_burn(double burn_duration,
                                  double &spacecraft_fuel_reserve) {
    return execute_tsiolkovsky_fire(thrust_newtons, specific_impulse_sec, burn_duration,
                                   fuel_mass_kg, spacecraft_fuel_reserve, operational_status);
}

/* ============================================================================
 * ThrusterDispatcher Implementation (Composition & Polymorphic Fallback)
 * ============================================================================ */

ThrusterDispatcher::ThrusterDispatcher() {
    // Hierarchy: 0 = IonThruster (High Isp primary), 1 = ChemicalThruster (High thrust secondary),
    //            2 = ColdGasThruster (Micro-adjustment fallback)
    engine_hierarchy[0] = std::make_unique<IonThruster>();
    engine_hierarchy[1] = std::make_unique<ChemicalThruster>();
    engine_hierarchy[2] = std::make_unique<ColdGasThruster>();
}

bool ThrusterDispatcher::arbitrate_and_dispatch(double required_delta_v,
                                               double time_to_closest_approach,
                                               double spacecraft_mass,
                                               BurnSolution& out_solution,
                                               ThrusterEngine*& out_selected_engine) {
    out_selected_engine = nullptr;
    out_solution = BurnSolution{};
    out_solution.delta_v_ms = required_delta_v;
    out_solution.feasible = false;

    // Polymorphic evaluation loop over ThrusterEngine* base pointers
    for (const auto& engine : engine_hierarchy) {
        if (!engine || !engine->is_operational()) {
            continue;
        }

        // Virtual dispatch to concrete is_feasible()
        if (engine->is_feasible(required_delta_v, time_to_closest_approach, spacecraft_mass)) {
            // Virtual dispatch to concrete calculate_burn()
            out_solution = engine->calculate_burn(required_delta_v, spacecraft_mass);
            if (out_solution.feasible) {
                out_selected_engine = engine.get();
                return true;
            }
        }
    }

    out_solution.diagnostics = "ALL_ENGINES_INFEASIBLE: Automated fallback exhausted across Ion, Chemical, and Cold Gas.";
    return false;
}

} // namespace ocas
