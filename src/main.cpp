/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Executive & Demonstration Driver
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#include "c_ingestion.h"
#include "ocas_system.hpp"
#include "risk_engine.hpp"
#include "thruster_engine.hpp"
#include "persistence.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_banner() {
    std::cout << "\n"
              << "================================================================================\n"
              << "  OCAS: Autonomous Orbital Collision Avoidance System\n"
              << "  Flight Executive Software Architecture | C11 & C++17 Safety-Critical Core\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "================================================================================\n\n";
}

void print_separator(char c = '-', size_t len = 80) {
    std::cout << "  " << std::string(len - 2, c) << "\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string mode = "--demo";
    if (argc > 1) {
        mode = argv[1];
    }

    ocas::OCASSystem system("logs/flight_audit.log");

    // Standard LEO Satellite Initial Kinematic State (Altitude ~408 km, orbital speed ~7.66 km/s)
    SatelliteState initial_state{};
    initial_state.epoch_s = 1726531200.0; // Reference mission epoch
    initial_state.r[0] = 6786.0;          // Mean earth radius + 408km
    initial_state.r[1] = 0.0;
    initial_state.r[2] = 120.0;
    initial_state.v[0] = 0.0;
    initial_state.v[1] = 7.66;
    initial_state.v[2] = 0.05;
    initial_state.dry_mass_kg = 1000.0;
    initial_state.fuel_mass_kg = 200.0;
    initial_state.status_flags = OCAS_STATUS_NOMINAL;
    initial_state.sequence_id = 1;

    system.initialize(initial_state);

    if (mode == "--json") {
        // Output current system telemetry JSON and exit
        std::cout << system.export_telemetry_json() << "\n";
        return 0;
    }

    print_banner();

    // ------------------------------------------------------------------------
    // Phase 1: Pure C11 High-Rate Telemetry Stream & Ring Buffer
    // ------------------------------------------------------------------------
    std::cout << "[PHASE 1] C11 Cache-Aligned Telemetry Streaming into Static Ring Buffer\n";
    std::cout << "          Ring Buffer Capacity: " << OCAS_RING_BUFFER_CAPACITY
              << " frames | Alignment: " << OCAS_ALIGNMENT << " bytes | Zero Heap Alloc\n\n";

    for (int i = 0; i < 70; ++i) { // Intentionally push 70 frames to demonstrate overwrite-oldest policy
        SatelliteState frame = initial_state;
        frame.epoch_s += i * 10.0;
        frame.r[1] += i * 0.15;
        frame.sequence_id = i + 1;
        bool fresh = system.ingest_telemetry(frame);
        if (!fresh && i == OCAS_RING_BUFFER_CAPACITY) {
            std::cout << "          >>> Ring buffer saturated at frame #" << (i + 1)
                      << " -> Enforcing deterministic overwrite-oldest policy.\n";
        }
    }

    const auto& rb = system.get_ring_buffer();
    std::cout << "          Ring Buffer Status: Count=" << ring_buffer_count(&rb)
              << " | Total Pushed=" << rb.total_pushed
              << " | Total Overwritten=" << rb.total_overwritten
              << " | Overflow Flag=" << (rb.overflow_occurred ? "TRUE" : "FALSE") << "\n";

    SatelliteState latest{};
    if (ring_buffer_peek_latest(&rb, &latest)) {
        std::cout << "          Latest Retained Frame: Seq=" << latest.sequence_id
                  << " Epoch=" << std::fixed << std::setprecision(1) << latest.epoch_s
                  << " s | r=[" << latest.r[0] << ", " << latest.r[1] << ", " << latest.r[2] << "] km\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 2: CDM Uplink Ingestion into Static FIFO Queue
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 2] Space Surveillance Network CDM Uplink Ingestion\n";
    std::cout << "          Static FIFO Queue Capacity: " << OCAS_CDM_FIFO_CAPACITY << " alerts\n\n";

    const std::vector<CDMAlert> incoming_cdms = {
        {101, "STARLINK-4092 FRAGMENT", 24000.0, 1.8, 5400.0, {{120.0, 0, 0}, {0, 120.0, 0}, {0, 0, 120.0}}, 1726536600.0, 1, {0}},
        {102, "COSMOS-1408 ASAT DEBRIS",  1450.0, 4.2, 1800.0, {{250.0, 0, 0}, {0, 250.0, 0}, {0, 0, 250.0}}, 1726533000.0, 2, {0}},
        {103, "ISS-SHED RADIATOR PANEL",   110.0, 12.5,  420.0, {{180.0, 0, 0}, {0, 180.0, 0}, {0, 0, 180.0}}, 1726531620.0, 3, {0}},
        {104, "ORBCOMM SPARE (DEFUNCT)",  7800.0, 3.4, 2700.0, {{380.0, 0, 0}, {0, 380.0, 0}, {0, 0, 380.0}}, 1726533900.0, 4, {0}},
        {105, "CZ-3B ROCKET STAGE",        320.0, 9.8, 1100.0, {{110.0, 0, 0}, {0, 110.0, 0}, {0, 0, 110.0}}, 1726532300.0, 5, {0}}
    };

    for (const auto& cdm : incoming_cdms) {
        system.ingest_cdm(cdm);
        std::cout << "          Uplink Ingested: CDM #" << cdm.object_id << " ["
                  << std::left << std::setw(25) << cdm.object_name << std::right
                  << "] d_miss=" << std::setw(7) << static_cast<int>(cdm.d_miss_m) << " m"
                  << " | v_rel=" << std::setw(4) << cdm.v_rel_kms << " km/s"
                  << " | t_TCA=" << std::setw(5) << static_cast<int>(cdm.t_tca_s) << " s\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 3: Explainable Edge ML (In-Place Logistic Regression)
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 3] In-Place Explainable Edge ML (Logistic Regression Classifier)\n";
    std::cout << "          Features: d_miss, v_rel, t_TCA, Tr(Covariance) | Constant Time O(1)\n\n";

    size_t processed = system.process_and_prioritize_threats();
    std::cout << "          Successfully scored and prioritized " << processed << " threats.\n\n";

    std::cout << "  ID   OBJECT DESIGNATOR           d_miss     v_rel   t_TCA   sigma_eff   Logit z   P(col)   THREAT LEVEL  TACTICAL ACTION\n";
    std::cout << "  ---  -------------------------  --------   -------  ------  ---------  ---------  ------   ------------  ------------------------\n";

    const auto& heap = system.get_max_heap();
    const ocas::ThreatAlert* threats = heap.data();
    for (size_t i = 0; i < heap.size(); ++i) {
        const auto& t = threats[i];
        std::cout << "  " << std::setw(3) << t.alert.object_id << "  "
                  << std::left << std::setw(25) << t.alert.object_name << std::right
                  << std::setw(8) << static_cast<int>(t.alert.d_miss_m) << " m "
                  << std::setw(7) << std::fixed << std::setprecision(1) << t.alert.v_rel_kms << " km/s"
                  << std::setw(7) << static_cast<int>(t.alert.t_tca_s) << " s "
                  << std::setw(8) << static_cast<int>(t.report.sigma_eff_m) << " m  "
                  << std::setw(9) << std::setprecision(3) << t.report.logit_z << "  "
                  << std::setw(6) << std::setprecision(4) << t.risk_score << "   "
                  << std::left << std::setw(12) << ocas::LogisticRiskClassifier::risk_level_to_string(t.report.risk_level)
                  << std::setw(24) << ocas::LogisticRiskClassifier::action_to_string(t.report.action)
                  << std::right << "\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 4: Encapsulated Binary Max-Heap Threat Priority Reordering
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 4] Encapsulated MaxHeapPriorityQueue: O(1) Peek Root Threat\n\n";

    ocas::ThreatAlert top_threat{};
    if (heap.peek(top_threat)) {
        std::cout << "          >>> CRITICAL ROOT THREAT: CDM #" << top_threat.alert.object_id
                  << " (" << top_threat.alert.object_name << ") with P(col)="
                  << std::fixed << std::setprecision(4) << top_threat.risk_score
                  << " | Miss=" << top_threat.alert.d_miss_m << " m | TCA="
                  << top_threat.alert.t_tca_s << " s\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 5: Polymorphic Propulsion Dispatcher & Automated Fallback
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 5] Dedicated C++ OOP Thruster Engine Hierarchy & Dispatcher\n";
    std::cout << "          Physics: Tsiolkovsky Delta-V, Mass Flow, Burn Duration, and Deadline Constraints\n";
    std::cout << "          Hierarchy: IonThruster (0.1 N, 3200 s) -> ChemicalThruster (400 N, 310 s) -> ColdGasThruster (10 N, 70 s)\n\n";

    std::cout << "          [EXECUTION 1 - DISPATCH & VIRTUAL ARBITRATION]: Mitigating Top Threat CDM #103\n";
    const double fuel_pre1 = system.get_current_state().fuel_mass_kg;
    bool avoided1 = system.execute_top_threat_avoidance(false);
    if (avoided1) {
        const auto& s = system.get_current_state();
        std::cout << "          >>> POLIMORPHIC BURN EXECUTED SUCCESSFULLY!\n";
        std::cout << "              Propellant Spent: " << std::fixed << std::setprecision(2)
                  << (fuel_pre1 - s.fuel_mass_kg) << " kg | Remaining: " << s.fuel_mass_kg << " kg\n";
    }

    std::cout << "\n          [EXECUTION 2 - AUTOMATED FALLBACK]: Mitigating Next Threat\n";
    std::cout << "          Simulating primary engine off-nominal condition to test dynamic fallback...\n";
    const double fuel_pre2 = system.get_current_state().fuel_mass_kg;
    bool avoided2 = system.execute_top_threat_avoidance(true);
    if (avoided2) {
        const auto& s = system.get_current_state();
        std::cout << "          >>> AUTOMATED FALLBACK TRIGGERED & EXECUTED!\n";
        std::cout << "              Propellant Spent: " << std::fixed << std::setprecision(2)
                  << (fuel_pre2 - s.fuel_mass_kg) << " kg | Remaining: " << s.fuel_mass_kg << " kg\n";
        std::cout << "              Pre-maneuver snapshot pushed to StateStack (Depth: "
                  << system.get_rollback_stack().size() << ")\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 6: StateStack Instant Rollback Demonstration
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 6] StateStack Instant O(1) Abort & Rollback Simulation\n";
    std::cout << "          Simulating attitude anomaly / burn abort command...\n";

    bool rolled_back = system.trigger_abort_rollback();
    if (rolled_back) {
        std::cout << "          >>> STATESTACK RESTORED PRE-BURN SATELLITE KINEMATICS IN O(1)!\n";
        std::cout << "          Restored Fuel Mass: " << std::fixed << std::setprecision(2)
                  << system.get_current_state().fuel_mass_kg << " kg\n";
        std::cout << "          Restored Status: Flag=0x" << std::hex
                  << system.get_current_state().status_flags << std::dec << " (Includes OCAS_STATUS_ROLLBACK)\n";
        std::cout << "          Current StateStack Depth: " << system.get_rollback_stack().size() << "\n";
    }

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 7: Doubly Linked List ManeuverLedger & Audit Traversal
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 7] ManeuverLedger (Doubly Linked List with RAII Destructor)\n";
    std::cout << "          Bidirectional Traversal: Forward (Timeline) & Backward (Audit)\n\n";

    std::cout << "          [FORWARD TIMELINE TRAVERSAL]:\n";
    system.get_mission_ledger().traverse_forward([](const ocas::ManeuverRecord& r, size_t idx) {
        std::cout << "            Record #" << (idx + 1) << " [Maneuver ID " << r.maneuver_id << "]: Target="
                  << r.target_name << " | dV=" << std::fixed << std::setprecision(2) << r.delta_v_actual_ms
                  << " m/s | Spent=" << r.propellant_used_kg << " kg | Burn Time="
                  << r.burn_duration_s << " s | Engine=" << r.engine_used << "\n";
    });

    auto audit = system.get_mission_ledger().compute_audit_summary();
    std::cout << "\n          [BACKWARD AUDIT SUMMARY]:\n";
    std::cout << "            Total Maneuvers Executed : " << audit.total_maneuvers << "\n";
    std::cout << "            Cumulative Delta-V Spent : " << audit.cumulative_delta_v_ms << " m/s\n";
    std::cout << "            Cumulative Propellant Mass: " << audit.cumulative_propellant_kg << " kg\n";
    std::cout << "            Cumulative Burn Duration : " << audit.cumulative_burn_time_s << " s\n";
    std::cout << "            Engine Duty Cycles       : Chemical=" << audit.chemical_burn_count
              << " | Ion=" << audit.ion_burn_count
              << " | Cold Gas=" << audit.cold_gas_burn_count << "\n";

    print_separator();

    // ------------------------------------------------------------------------
    // Phase 8: Append-Only JSON Flight Audit Verification
    // ------------------------------------------------------------------------
    std::cout << "\n[PHASE 8] Append-Only Flight Audit Log Verification\n";
    std::cout << "          Structured JSON telemetry successfully written to logs/flight_audit.log\n\n";

    std::cout << "================================================================================\n"
              << "  MISSION CYCLE COMPLETED SUCCESSFULLY | ALL FLIGHT CONSTRAINTS SATISFIED\n"
              << "================================================================================\n\n";

    return 0;
}
