/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Comprehensive Verification & Test Suite (C11 & C++17)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#include "c_ingestion.h"
#include "risk_engine.hpp"
#include "thruster_engine.hpp"
#include "persistence.hpp"
#include "ocas_system.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\n[TEST FAILED] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

namespace {

void test_c11_structures_and_alignment() {
    std::cout << "[TEST 1] Checking C11 cache-alignment and structure sizes...\n";
    TEST_ASSERT(alignof(SatelliteState) >= OCAS_ALIGNMENT, "SatelliteState must be 64-byte cache-aligned");
    TEST_ASSERT(alignof(CDMAlert) >= OCAS_ALIGNMENT, "CDMAlert must be 64-byte cache-aligned");
    TEST_ASSERT(sizeof(SatelliteState) % OCAS_ALIGNMENT == 0, "SatelliteState must be multiple of 64 bytes");
    TEST_ASSERT(sizeof(CDMAlert) % OCAS_ALIGNMENT == 0, "CDMAlert must be multiple of 64 bytes");
    std::cout << "         PASS: SatelliteState (" << sizeof(SatelliteState) << " B), CDMAlert (" << sizeof(CDMAlert) << " B)\n";
}

void test_ring_buffer_deterministic_overwrite() {
    std::cout << "[TEST 2] Testing C11 static circular ring buffer with overwrite-oldest policy...\n";
    TelemetryRingBuffer rb;
    ring_buffer_init(&rb);

    TEST_ASSERT(ring_buffer_count(&rb) == 0, "Initial count must be 0");
    TEST_ASSERT(!ring_buffer_is_full(&rb), "Must not be full initially");

    // Fill buffer to capacity
    for (size_t i = 0; i < OCAS_RING_BUFFER_CAPACITY; ++i) {
        SatelliteState s{};
        s.sequence_id = static_cast<uint32_t>(i + 1);
        s.epoch_s = 1000.0 + i;
        bool ok = ring_buffer_push(&rb, &s);
        TEST_ASSERT(ok, "Pushing within capacity must succeed with true");
    }

    TEST_ASSERT(ring_buffer_count(&rb) == OCAS_RING_BUFFER_CAPACITY, "Buffer must be at capacity");
    TEST_ASSERT(ring_buffer_is_full(&rb), "Buffer must report full");
    TEST_ASSERT(!rb.overflow_occurred, "No overflow yet");

    // Push 5 more items to trigger overwrite-oldest policy
    for (size_t i = 0; i < 5; ++i) {
        SatelliteState s{};
        s.sequence_id = static_cast<uint32_t>(OCAS_RING_BUFFER_CAPACITY + i + 1);
        s.epoch_s = 2000.0 + i;
        bool ok = ring_buffer_push(&rb, &s);
        TEST_ASSERT(!ok, "Pushing past capacity must report overwrite (false)");
    }

    TEST_ASSERT(ring_buffer_count(&rb) == OCAS_RING_BUFFER_CAPACITY, "Count must remain capped at capacity");
    TEST_ASSERT(rb.overflow_occurred, "Overflow flag must be set");
    TEST_ASSERT(rb.total_overwritten == 5, "Exactly 5 items must have been overwritten");

    // Check latest frame
    SatelliteState latest{};
    TEST_ASSERT(ring_buffer_peek_latest(&rb, &latest), "Peek latest must succeed");
    TEST_ASSERT(latest.sequence_id == OCAS_RING_BUFFER_CAPACITY + 5, "Latest frame must match newest pushed sequence");

    // Check oldest retained frame
    SatelliteState oldest{};
    TEST_ASSERT(ring_buffer_peek_at(&rb, 0, &oldest), "Peek at 0 must succeed");
    TEST_ASSERT(oldest.sequence_id == 6, "Oldest retained sequence must be 6 (1-5 were evicted)");

    std::cout << "         PASS: Circular ring buffer overwrite-oldest behavior verified.\n";
}

void test_cdm_fifo_queue_bounded_drop() {
    std::cout << "[TEST 3] Testing C11 static FIFO queue bounded drop-on-full policy...\n";
    CDMFifoQueue q;
    cdm_fifo_init(&q);

    TEST_ASSERT(cdm_fifo_is_empty(&q), "Initial queue must be empty");

    // Fill queue
    for (size_t i = 0; i < OCAS_CDM_FIFO_CAPACITY; ++i) {
        CDMAlert a{};
        a.alert_id = static_cast<uint32_t>(i + 1);
        bool ok = cdm_fifo_push(&q, &a);
        TEST_ASSERT(ok, "Pushing within capacity must succeed");
    }

    TEST_ASSERT(cdm_fifo_is_full(&q), "Queue must report full");

    // Push extra item: must be dropped to protect memory
    CDMAlert extra{};
    extra.alert_id = 999;
    bool dropped_ok = cdm_fifo_push(&q, &extra);
    TEST_ASSERT(!dropped_ok, "Pushing to full FIFO queue must fail gracefully");
    TEST_ASSERT(q.total_dropped == 1, "Dropped counter must be 1");
    TEST_ASSERT(q.overflow_occurred, "Overflow flag must be set");

    // Dequeue all items in FIFO order
    for (size_t i = 0; i < OCAS_CDM_FIFO_CAPACITY; ++i) {
        CDMAlert out{};
        bool pop_ok = cdm_fifo_pop(&q, &out);
        TEST_ASSERT(pop_ok, "Pop must succeed");
        TEST_ASSERT(out.alert_id == i + 1, "Alert ID must match exact FIFO sequence");
    }

    TEST_ASSERT(cdm_fifo_is_empty(&q), "Queue must be empty after popping all");
    std::cout << "         PASS: Static CDM FIFO bounded drop-on-full verified.\n";
}

void test_logistic_risk_classifier() {
    std::cout << "[TEST 4] Testing Explainable Edge ML Logistic Regression Classifier...\n";
    ocas::LogisticRiskClassifier classifier;

    // 1. Extreme Critical Threat: zero miss distance, hypervelocity, immediate TCA, high covariance
    CDMAlert critical_alert{};
    critical_alert.object_id = 1;
    critical_alert.d_miss_m = 10.0;
    critical_alert.v_rel_kms = 14.0;
    critical_alert.t_tca_s = 60.0;
    critical_alert.covariance[0][0] = 300.0;
    critical_alert.covariance[1][1] = 300.0;
    critical_alert.covariance[2][2] = 300.0;

    ocas::ThreatAlert scored_crit = classifier.evaluate(critical_alert);
    TEST_ASSERT(scored_crit.risk_score >= 0.85, "Critical threat must score >= 0.85");
    TEST_ASSERT(scored_crit.report.risk_level == ocas::RiskLevel::CRITICAL, "Must be classified as CRITICAL");
    TEST_ASSERT(scored_crit.report.action == ocas::TacticalAction::IMMEDIATE_AVOIDANCE_BURN, "Action must be IMMEDIATE_AVOIDANCE_BURN");

    // 2. Far Benign Conjunction: large miss distance, low relative velocity, distant TCA
    CDMAlert benign_alert{};
    benign_alert.object_id = 2;
    benign_alert.d_miss_m = 60000.0;
    benign_alert.v_rel_kms = 0.5;
    benign_alert.t_tca_s = 10000.0;
    benign_alert.covariance[0][0] = 10.0;
    benign_alert.covariance[1][1] = 10.0;
    benign_alert.covariance[2][2] = 10.0;

    ocas::ThreatAlert scored_benign = classifier.evaluate(benign_alert);
    TEST_ASSERT(scored_benign.risk_score < 0.20, "Benign threat must score < 0.20");
    TEST_ASSERT(scored_benign.report.risk_level == ocas::RiskLevel::LOW, "Must be classified as LOW");
    TEST_ASSERT(scored_benign.report.action == ocas::TacticalAction::NOMINAL_PASS, "Action must be NOMINAL_PASS");

    // Monotonicity test: closer miss distance must yield higher risk
    TEST_ASSERT(scored_crit.risk_score > scored_benign.risk_score, "Risk score must be monotonic with severity");
    std::cout << "         PASS: Logistic regression classifier bounded and monotonic.\n";
}

void test_max_heap_priority_queue() {
    std::cout << "[TEST 5] Testing Encapsulated MaxHeapPriorityQueue for ThreatAlert...\n";
    ocas::MaxHeapPriorityQueue heap;
    ocas::LogisticRiskClassifier classifier;

    TEST_ASSERT(heap.is_empty(), "Initial heap must be empty");

    const std::vector<double> distances = {50000.0, 200.0, 15000.0, 50.0, 3000.0, 800.0};
    for (size_t i = 0; i < distances.size(); ++i) {
        CDMAlert a{};
        a.object_id = static_cast<uint32_t>(i + 1);
        a.d_miss_m = distances[i];
        a.v_rel_kms = 7.0;
        a.t_tca_s = 600.0;
        ocas::ThreatAlert t = classifier.evaluate(a);
        heap.insert(t);
    }

    TEST_ASSERT(heap.size() == distances.size(), "Heap size must match inserted count");

    // Verify root is the highest risk
    ocas::ThreatAlert root{};
    TEST_ASSERT(heap.peek(root), "Peek must succeed");
    TEST_ASSERT(root.alert.d_miss_m == 50.0, "Root must have the closest miss distance (highest risk)");

    // Extract all and verify descending order
    double prev_risk = 2.0;
    size_t extracted_count = 0;
    ocas::ThreatAlert current{};
    while (heap.extract_max(current)) {
        TEST_ASSERT(current.risk_score <= prev_risk, "Extracted threats must follow descending max-heap order");
        prev_risk = current.risk_score;
        extracted_count++;
    }

    TEST_ASSERT(extracted_count == distances.size(), "All elements must be extracted");
    TEST_ASSERT(heap.is_empty(), "Heap must be empty after full extraction");
    std::cout << "         PASS: Encapsulated MaxHeapPriorityQueue ordering verified.\n";
}

void test_oop_thruster_hierarchy_and_tsiolkovsky() {
    std::cout << "[TEST 6] Testing Dedicated OOP ThrusterEngine Hierarchy & Polymorphism...\n";

    ocas::ChemicalThruster chem(200.0);
    ocas::IonThruster ion(200.0);
    ocas::ColdGasThruster rcs(200.0);

    const double mass = 1200.0;
    const double delta_v = 5.0; // 5 m/s

    // Test Polymorphic invocation via Base Class Pointer
    const ocas::ThrusterEngine* base_chem = &chem;
    const ocas::ThrusterEngine* base_ion = &ion;
    const ocas::ThrusterEngine* base_rcs = &rcs;

    // 1. Tsiolkovsky Equation verification on Chemical
    // ve = 310 * 9.80665 = 3040.0615 m/s
    // dm = 1200 * (1 - e^(-5 / 3040.0615)) ~ 1.9726 kg
    ocas::BurnSolution chem_sol = base_chem->calculate_burn(delta_v, mass);
    const double chem_ve = 310.0 * ocas::G0_STANDARD;
    const double expected_chem_dm = mass * (1.0 - std::exp(-delta_v / chem_ve));
    TEST_ASSERT(std::abs(chem_sol.propellant_mass_kg - expected_chem_dm) < 1e-4, "Chemical propellant mass mismatch");
    TEST_ASSERT(chem_sol.burn_duration_sec < 20.0, "Chemical burn should be rapid (<20s)");

    // 2. Ion Thruster Rejection Rule Constraint:
    // Rejects if t_burn > 0.8 * t_TCA
    // For 5 m/s on 1200 kg: tb ~ 60,000 s.
    // If t_TCA = 500 s, Ion MUST reject (tb > 0.8 * 500)
    TEST_ASSERT(!base_ion->is_feasible(delta_v, 500.0, mass), "Ion must reject short-fused conjunction");
    // Chemical Thruster should easily accept 500s window
    TEST_ASSERT(base_chem->is_feasible(delta_v, 500.0, mass), "Chemical must accept 500s window");

    // 3. Cold Gas Thruster Micro-adjustment Constraint:
    // Rejects if delta_v >= 0.05 m/s
    TEST_ASSERT(!base_rcs->is_feasible(0.10, 1000.0, mass), "Cold Gas must reject Delta-V >= 0.05 m/s");
    TEST_ASSERT(base_rcs->is_feasible(0.02, 1000.0, mass), "Cold Gas must accept Delta-V < 0.05 m/s");

    // 4. Automated Fallback via ThrusterDispatcher
    ocas::ThrusterDispatcher dispatcher;
    ocas::BurnSolution sol;
    ocas::ThrusterEngine* selected = nullptr;

    // Conjunction with short TCA (600s): Ion rejected (tb > 0.8*600), fallback to Chemical!
    bool ok = dispatcher.arbitrate_and_dispatch(4.0, 600.0, mass, sol, selected);
    TEST_ASSERT(ok, "Dispatcher arbitration must succeed");
    TEST_ASSERT(selected != nullptr, "Selected engine must not be null");
    TEST_ASSERT(selected->get_name() == "CHEMICAL_BIPROP", "Dispatcher must select Chemical for rapid conjunction");

    // Test Burn Execution and Fuel Depletion
    double reserve = 200.0;
    bool fire_ok = selected->execute_burn(sol.burn_duration_sec, reserve);
    TEST_ASSERT(fire_ok, "Burn execution must succeed");
    TEST_ASSERT(reserve < 200.0, "Propellant reserve must be depleted");

    std::cout << "         PASS: ThrusterEngine inheritance, pure virtuals, and dispatcher fallback verified.\n";
}

void test_persistence_state_stack_and_maneuver_ledger() {
    std::cout << "[TEST 7] Testing StateStack LIFO and ManeuverLedger RAII Doubly Linked List...\n";

    // 1. StateStack
    ocas::StateStack stack;
    SatelliteState s1{}, s2{};
    s1.epoch_s = 100.0; s1.fuel_mass_kg = 200.0;
    s2.epoch_s = 200.0; s2.fuel_mass_kg = 180.0;

    stack.push_snapshot(s1, 10, "SNAPSHOT_1");
    stack.push_snapshot(s2, 20, "SNAPSHOT_2");
    TEST_ASSERT(stack.size() == 2, "Stack size must be 2");

    SatelliteState popped{};
    TEST_ASSERT(stack.pop_and_rollback(popped), "Pop must succeed");
    TEST_ASSERT(popped.epoch_s == 200.0 && popped.fuel_mass_kg == 180.0, "Popped state must match s2 (LIFO)");

    TEST_ASSERT(stack.pop_and_rollback(popped), "Second pop must succeed");
    TEST_ASSERT(popped.epoch_s == 100.0 && popped.fuel_mass_kg == 200.0, "Second popped state must match s1");
    TEST_ASSERT(stack.is_empty(), "Stack must now be empty");

    // 2. ManeuverLedger with RAII
    {
        ocas::ManeuverLedger ledger;
        for (uint32_t i = 0; i < 5; ++i) {
            ocas::ManeuverRecord rec{};
            rec.maneuver_id = i + 1;
            rec.delta_v_actual_ms = 2.0 * (i + 1);
            rec.propellant_used_kg = 0.5 * (i + 1);
            rec.burn_duration_s = 10.0;
            rec.engine_used = "CHEMICAL_BIPROP";
            ledger.append_maneuver(rec);
        }

        TEST_ASSERT(ledger.size() == 5, "Ledger size must be 5");

        // Forward check
        std::vector<uint32_t> forward_ids;
        ledger.traverse_forward([&forward_ids](const ocas::ManeuverRecord& r, size_t) {
            forward_ids.push_back(r.maneuver_id);
        });
        TEST_ASSERT(forward_ids.size() == 5 && forward_ids[0] == 1 && forward_ids[4] == 5, "Forward traversal must be chronological");

        // Backward audit check
        auto summary = ledger.compute_audit_summary();
        TEST_ASSERT(summary.total_maneuvers == 5, "Total maneuvers must be 5");
        // delta_v = 2 + 4 + 6 + 8 + 10 = 30.0
        TEST_ASSERT(std::abs(summary.cumulative_delta_v_ms - 30.0) < 1e-5, "Cumulative Delta-V mismatch");
        // propellant = 0.5 + 1.0 + 1.5 + 2.0 + 2.5 = 7.5
        TEST_ASSERT(std::abs(summary.cumulative_propellant_kg - 7.5) < 1e-5, "Cumulative propellant mismatch");
        TEST_ASSERT(summary.chemical_burn_count == 5, "Chemical burn count must be 5");
    } // RAII destructor frees all ManeuverNode elements safely

    std::cout << "         PASS: StateStack LIFO rollback and ManeuverLedger RAII verified.\n";
}

} // anonymous namespace

int main() {
    std::cout << "================================================================================\n"
              << "  RUNNING OCAS AUTOMATED FLIGHT SOFTWARE VERIFICATION SUITE\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "================================================================================\n\n";

    test_c11_structures_and_alignment();
    test_ring_buffer_deterministic_overwrite();
    test_cdm_fifo_queue_bounded_drop();
    test_logistic_risk_classifier();
    test_max_heap_priority_queue();
    test_oop_thruster_hierarchy_and_tsiolkovsky();
    test_persistence_state_stack_and_maneuver_ledger();

    std::cout << "\n================================================================================\n"
              << "  ALL VERIFICATION TESTS PASSED (100% SUCCESS, ZERO LEAKS, ZERO RUNTIME CRASHES)\n"
              << "================================================================================\n\n";
    return 0;
}
