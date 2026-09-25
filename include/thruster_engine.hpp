/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Polymorphic Propulsion Dispatcher & Thruster Hierarchy (C++17)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Implements strict OOP principles:
 *  - Encapsulation (protected member variables, public interface)
 *  - Inheritance & Runtime Polymorphism (virtual dispatch via vtable)
 *  - Abstract Classes (pure virtual interface contracts)
 *  - RAII via std::unique_ptr in ThrusterDispatcher
 * ============================================================================
 */

#ifndef OCAS_THRUSTER_ENGINE_HPP
#define OCAS_THRUSTER_ENGINE_HPP

#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace ocas {

constexpr double G0_STANDARD = 9.80665; // Standard acceleration of gravity [m/s^2]

/**
 * @brief Telemetry solution computed from Tsiolkovsky's Rocket Equation.
 */
struct BurnSolution {
    bool feasible = false;
    double delta_v_ms = 0.0;           /**< Target velocity increment [m/s] */
    double propellant_mass_kg = 0.0;   /**< Required propellant mass Delta-m [kg] */
    double burn_duration_sec = 0.0;    /**< Total thruster firing duration [s] */
    double mass_flow_rate_kgs = 0.0;   /**< Mass flow rate m_dot [kg/s] */
    std::string engine_name;           /**< Identifier of evaluating engine */
    std::string diagnostics;           /**< Reason for feasibility or rejection */
};

/**
 * @brief Abstract Base Class: ThrusterEngine (Hardware Interface Abstraction).
 * Establishes a hardware-agnostic contract for all spacecraft propulsion systems.
 */
class ThrusterEngine {
protected:
    std::string engine_name;
    double thrust_newtons;
    double specific_impulse_sec;
    double min_pulse_time_sec;
    double fuel_mass_kg;
    bool operational_status;

public:
    ThrusterEngine(std::string name,
                   double thrust_n,
                   double isp_s,
                   double min_pulse_s,
                   double initial_fuel_kg,
                   bool operational = true) noexcept
        : engine_name(std::move(name)),
          thrust_newtons(thrust_n),
          specific_impulse_sec(isp_s),
          min_pulse_time_sec(min_pulse_s),
          fuel_mass_kg(initial_fuel_kg),
          operational_status(operational) {}

    virtual ~ThrusterEngine() = default;

    /**
     * @brief Validates whether the engine can deliver the necessary Delta-V without
     * exceeding fuel limits or burn duration before t_TCA.
     */
    virtual bool is_feasible(double required_delta_v,
                             double time_to_closest_approach,
                             double spacecraft_mass) const = 0;

    /**
     * @brief Applies Tsiolkovsky's rocket equation to compute required propellant mass
     * Delta-m and burn time t_burn.
     */
    virtual BurnSolution calculate_burn(double required_delta_v,
                                        double spacecraft_mass) const = 0;

    /**
     * @brief Simulates firing, depleting fuel reserves, checking thruster health flags,
     * and returning success or mid-burn abort.
     */
    virtual bool execute_burn(double burn_duration,
                              double &spacecraft_fuel_reserve) = 0;

    // Common Getters & Telemetry Interfaces
    [[nodiscard]] const std::string& get_name() const noexcept { return engine_name; }
    [[nodiscard]] double get_thrust() const noexcept { return thrust_newtons; }
    [[nodiscard]] double get_isp() const noexcept { return specific_impulse_sec; }
    [[nodiscard]] double get_min_pulse() const noexcept { return min_pulse_time_sec; }
    [[nodiscard]] double get_fuel_mass() const noexcept { return fuel_mass_kg; }
    [[nodiscard]] bool is_operational() const noexcept { return operational_status; }

    void set_operational(bool status) noexcept { operational_status = status; }
    void replenish_fuel(double fuel_kg) noexcept { fuel_mass_kg = fuel_kg; }
};

/**
 * @brief Concrete Derived Class: Chemical Bipropellant Thruster.
 * High thrust (400 N), low Isp (310 s), hypergolic bipropellant.
 */
class ChemicalThruster : public ThrusterEngine {
public:
    explicit ChemicalThruster(double initial_fuel_kg = 200.0) noexcept
        : ThrusterEngine("CHEMICAL_BIPROP", 400.0, 310.0, 0.05, initial_fuel_kg, true) {}

    bool is_feasible(double required_delta_v,
                     double time_to_closest_approach,
                     double spacecraft_mass) const override;

    BurnSolution calculate_burn(double required_delta_v,
                                double spacecraft_mass) const override;

    bool execute_burn(double burn_duration,
                      double &spacecraft_fuel_reserve) override;
};

/**
 * @brief Concrete Derived Class: Gridded Ion Thruster.
 * Micro-thrust (0.1 N), ultra-high Isp (3200 s), Xenon propellant.
 * Rejects maneuver if burn duration > 0.8 * t_TCA (short-fused conjunctions).
 */
class IonThruster : public ThrusterEngine {
public:
    explicit IonThruster(double initial_fuel_kg = 200.0) noexcept
        : ThrusterEngine("GRIDDED_ION", 0.1, 3200.0, 30.0, initial_fuel_kg, true) {}

    bool is_feasible(double required_delta_v,
                     double time_to_closest_approach,
                     double spacecraft_mass) const override;

    BurnSolution calculate_burn(double required_delta_v,
                                double spacecraft_mass) const override;

    bool execute_burn(double burn_duration,
                      double &spacecraft_fuel_reserve) override;
};

/**
 * @brief Concrete Derived Class: Cold Gas Reaction Control System (RCS).
 * Low thrust (10 N), low Isp (70 s). Used strictly for micro-adjustments (Delta-V < 0.05 m/s)
 * or attitude detumbling / backup maneuvers.
 */
class ColdGasThruster : public ThrusterEngine {
public:
    explicit ColdGasThruster(double initial_fuel_kg = 200.0) noexcept
        : ThrusterEngine("COLD_GAS_RCS", 10.0, 70.0, 0.01, initial_fuel_kg, true) {}

    bool is_feasible(double required_delta_v,
                     double time_to_closest_approach,
                     double spacecraft_mass) const override;

    BurnSolution calculate_burn(double required_delta_v,
                                double spacecraft_mass) const override;

    bool execute_burn(double burn_duration,
                      double &spacecraft_fuel_reserve) override;
};

/**
 * @brief Orchestration & Composition: ThrusterDispatcher Class.
 * Holds an internal hierarchy array via base-class unique_ptr and iterates
 * polymorphically to evaluate feasibility and execute automated fallback.
 */
class ThrusterDispatcher {
public:
    ThrusterDispatcher();
    ~ThrusterDispatcher() = default;

    // Disallow copy, allow move
    ThrusterDispatcher(const ThrusterDispatcher&) = delete;
    ThrusterDispatcher& operator=(const ThrusterDispatcher&) = delete;
    ThrusterDispatcher(ThrusterDispatcher&&) noexcept = default;
    ThrusterDispatcher& operator=(ThrusterDispatcher&&) noexcept = default;

    /**
     * @brief Evaluates propulsion hierarchy polymorphically.
     * Automated Fallback: If primary engine fails feasibility, automatically delegates
     * to the next viable engine in the hierarchy via virtual dispatch.
     */
    bool arbitrate_and_dispatch(double required_delta_v,
                                double time_to_closest_approach,
                                double spacecraft_mass,
                                BurnSolution& out_solution,
                                ThrusterEngine*& out_selected_engine);

    [[nodiscard]] const std::array<std::unique_ptr<ThrusterEngine>, 3>& get_hierarchy() const noexcept {
        return engine_hierarchy;
    }

    [[nodiscard]] std::array<std::unique_ptr<ThrusterEngine>, 3>& get_hierarchy() noexcept {
        return engine_hierarchy;
    }

private:
    std::array<std::unique_ptr<ThrusterEngine>, 3> engine_hierarchy;
};

} // namespace ocas

#endif /* OCAS_THRUSTER_ENGINE_HPP */
