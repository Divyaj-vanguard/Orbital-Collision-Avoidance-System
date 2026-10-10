// ============================================================================
// OCAS verification suite. Self-contained (no framework dependency).
// Each TEST is a function; CHECK records failures and continues.
// ============================================================================
#include "core/cdm_io.hpp"
#include "ingestion/circular_buffer.hpp"
#include "persistence/flight_audit_logger.hpp"
#include "persistence/mission_ledger.hpp"
#include "persistence/state_stack.hpp"
#include "pipeline/ocas_pipeline.hpp"
#include "priority/max_heap.hpp"
#include "regime/regime_strategy.hpp"
#include "risk/risk_strategy.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                    \
    do {                                                                                         \
        ++g_checks;                                                                              \
        const double va_ = (a), vb_ = (b);                                                       \
        if (!(std::fabs(va_ - vb_) <= (tol))) {                                                  \
            ++g_failures;                                                                        \
            std::printf("    FAIL %s:%d  %s = %.9g, expected %.9g\n", __FILE__, __LINE__, #a, va_, vb_); \
        }                                                                                        \
    } while (0)

using namespace ocas;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

ConjunctionTelemetry cdm(std::uint32_t id, double d, double v, double t, double cov, double pc = 0.0) {
    ConjunctionTelemetry c{};
    c.alert_id = id;
    c.miss_distance_m = d;
    c.relative_speed_km_s = v;
    c.time_to_tca_sec = t;
    c.cov_trace_m2 = cov;
    c.collision_prob = pc;
    return c;
}

// --------------------------------------------------------------------------
void test_circular_buffer() {
    CircularBuffer<int, 4> rb;
    CHECK(rb.empty() && rb.capacity() == 4);
    CHECK(alignof(decltype(rb)) == kCacheLineBytes);
    for (int i = 1; i <= 4; ++i) CHECK(rb.push(i) == PushResult::Stored);
    CHECK(rb.full() && !rb.overflow_occurred());
    CHECK(rb.push(5) == PushResult::OverwroteOldest);  // evicts 1
    CHECK(rb.push(6) == PushResult::OverwroteOldest);  // evicts 2
    CHECK(rb.overflow_count() == 2 && rb.total_pushed() == 6);
    CHECK(*rb.peek_oldest() == 3 && *rb.peek_newest() == 6);
    CHECK(rb.any_of([](int x) { return x == 5; }) && !rb.any_of([](int x) { return x == 1; }));
    int out = 0;
    for (int expect = 3; expect <= 6; ++expect) {
        CHECK(rb.pop(out) && out == expect);
    }
    CHECK(!rb.pop(out) && rb.empty() && rb.peek_oldest() == nullptr);
    // wrap-around after partial drain
    rb.push(7); rb.push(8); rb.pop(out); rb.push(9); rb.push(10); rb.push(11);
    CHECK(rb.size() == 4 && rb.at(0) == 8 && rb.at(3) == 11);
}

// --------------------------------------------------------------------------
struct IntLess {
    bool operator()(int a, int b) const noexcept { return a < b; }
};

void test_max_heap() {
    MaxHeap<int, 128, IntLess> h;
    unsigned lcg = 12345;
    for (int i = 0; i < 128; ++i) {
        lcg = lcg * 1103515245u + 12345u;
        CHECK(h.push(static_cast<int>((lcg >> 8) % 1000)));
    }
    CHECK(h.full() && h.is_valid_heap());
    CHECK(!h.push(5) && h.rejected_count() == 1);
    int prev = 1 << 30, x = 0, n = 0;
    bool sorted = true;
    const int top = *h.top();
    while (h.pop(x)) {
        if (n == 0 && x != top) sorted = false;
        if (x > prev) sorted = false;
        prev = x;
        ++n;
    }
    CHECK(sorted && n == 128 && h.top() == nullptr);

    // ThreatComparator semantics
    ThreatComparator less;
    ScoredThreat act{}, quiet{};
    act.risk.actionable = true;
    act.priority = 0.1;
    quiet.risk.actionable = false;
    quiet.priority = 0.9;
    CHECK(less(quiet, act) && !less(act, quiet));  // actionable outranks higher priority
    ScoredThreat soon = act, late = act;
    soon.telemetry.time_to_tca_sec = 100.0;
    late.telemetry.time_to_tca_sec = 900.0;
    CHECK(less(late, soon));  // equal priority: earlier TCA ranks higher

    ThreatHeap th;
    for (int i = 0; i < 10; ++i) {
        ScoredThreat s{};
        s.priority = 0.1 * i;
        s.risk.actionable = (i % 3 == 0);
        s.sequence = static_cast<std::uint64_t>(i);
        th.push(s);
    }
    ScoredThreat s{};
    th.pop(s);
    CHECK(s.risk.actionable && std::fabs(s.priority - 0.9) < 1e-12);
    CHECK(th.is_valid_heap());
}

// --------------------------------------------------------------------------
void test_logistic_strategy() {
    LogisticModelParams p;
    LogisticRegressionStrategy lr(p);
    const auto t = cdm(1, 500.0, 10.0, 200000.0, 4256.0 * 4256.0);
    const RiskAssessment a = lr.assess(t);
    CHECK(a.model == RiskModel::LogisticRegression);
    CHECK_NEAR(a.features[0], 1.0 - 500.0 / 1500.0, 1e-12);
    CHECK_NEAR(a.features[1], 10.0 / 22.26, 1e-12);
    CHECK_NEAR(a.features[2], std::exp(-200000.0 / 285689.0), 1e-12);
    CHECK_NEAR(a.features[3], 0.5, 1e-12);
    const double z = p.w_bias + a.contributions[0] + a.contributions[1] + a.contributions[2] + a.contributions[3];
    CHECK_NEAR(a.logit, z, 1e-12);
    CHECK_NEAR(a.score, 1.0 / (1.0 + std::exp(-z)), 1e-12);
    CHECK(a.actionable == (a.score >= p.decision_threshold));
    // Monotonic in miss distance (dominant positive weight)
    CHECK(lr.assess(cdm(1, 10.0, 10.0, 200000.0, 1e6)).score > lr.assess(cdm(1, 30000.0, 10.0, 200000.0, 1e6)).score);
    // Deterministic
    CHECK(lr.assess(t).score == a.score);
}

void test_conservative_strategy() {
    ConservativeThresholdStrategy c;
    // Missing covariance -> geometry only, 5 km screen, continuous at boundary
    RiskAssessment a = c.assess(cdm(1, 4000.0, 7.0, 1e5, kNaN));
    CHECK(a.rule == ConservativeRule::GeometricOnly && a.actionable);
    a = c.assess(cdm(1, 6000.0, 7.0, 1e5, kNaN));
    CHECK(!a.actionable && a.score < ConservativeThresholdStrategy::kActionScore);
    CHECK_NEAR(c.assess(cdm(1, 4999.999, 7.0, 1e5, kNaN)).score, ConservativeThresholdStrategy::kActionScore, 1e-6);
    // Pc estimate formula
    const double cov = 3.0 * 200.0 * 200.0;  // sigma = 200 m per axis
    const double expect = 400.0 / (2.0 * 40000.0) * std::exp(-(300.0 * 300.0) / (2.0 * 40000.0));
    CHECK_NEAR(ConservativeThresholdStrategy::estimate_pc(300.0, cov, 20.0), expect, 1e-15);
    a = c.assess(cdm(1, 1500.0, 7.0, 1e5, cov));
    CHECK(a.rule == ConservativeRule::PcEstimate && !a.actionable);  // Pc ~ 3.7e-14
    a = c.assess(cdm(1, 800.0, 7.0, 1e5, 1e10));
    CHECK(a.rule == ConservativeRule::ScreeningVolume && a.actionable && a.score >= 0.6);
    // Dilution: implausible covariance falls back to geometry
    CHECK(c.assess(cdm(1, 2000.0, 7.0, 1e5, 1e14)).rule == ConservativeRule::GeometricOnly);
}

void test_risk_classifier_selection() {
    RiskClassifier leo({}, {}, true);
    RiskClassifier geo({}, {}, false);
    CHECK(leo.select(cdm(1, 1000, 10, 2e5, 1e6)) == ModelSelection::InDomain);
    CHECK(leo.select(cdm(1, 1000, 10, 2e5, kNaN)) == ModelSelection::CovarianceMissing);
    CHECK(leo.select(cdm(1, 1000, 10, 2e5, 0.0)) == ModelSelection::CovarianceMissing);
    CHECK(leo.select(cdm(1, 1000, 10, 2e5, 1e14)) == ModelSelection::CovarianceImplausible);
    CHECK(leo.select(cdm(1, 1000, 20, 2e5, 1e6)) == ModelSelection::VelocityOutOfEnvelope);
    CHECK(geo.select(cdm(1, 1000, 1, 2e5, 1e6)) == ModelSelection::RegimeOutOfDomain);
    const RiskAssessment a = leo.assess(cdm(1, 1000, 10, 2e5, kNaN));
    CHECK(a.model == RiskModel::ConservativeThreshold && a.selection == ModelSelection::CovarianceMissing);
    CHECK(leo.assess(cdm(1, 1000, 10, 2e5, 1e6)).model == RiskModel::LogisticRegression);
}

// --------------------------------------------------------------------------
void test_regime_factory() {
    OrbitalRegime r{};
    CHECK(RegimeFactory::classify(550, r) && r == OrbitalRegime::LEO);
    CHECK(RegimeFactory::classify(1999.9, r) && r == OrbitalRegime::LEO);
    CHECK(RegimeFactory::classify(2000, r) && r == OrbitalRegime::MEO);
    CHECK(RegimeFactory::classify(20200, r) && r == OrbitalRegime::MEO);
    CHECK(RegimeFactory::classify(35586, r) && r == OrbitalRegime::GEO);
    CHECK(RegimeFactory::classify(35786, r) && r == OrbitalRegime::GEO);
    CHECK(!RegimeFactory::classify(50, r) && !RegimeFactory::classify(kNaN, r));
    CHECK(RegimeFactory::create_regime(-1) == nullptr);

    auto leo = RegimeFactory::create_regime(550);
    auto meo = RegimeFactory::create_regime(20200);
    auto geo = RegimeFactory::create_regime(35786);
    CHECK(leo && dynamic_cast<LEORegime*>(leo.get()) != nullptr);
    CHECK(meo && dynamic_cast<MEORegime*>(meo.get()) != nullptr);
    CHECK(geo && dynamic_cast<GEORegime*>(geo.get()) != nullptr);
    CHECK_NEAR(geo->orbital_period_s(), 86164.1, 1.0);   // sidereal day
    CHECK_NEAR(leo->orbital_period_s(), 5739.0, 5.0);    // ~95.7 min at 550 km

    // Regime risk: bounded, closer is riskier
    for (const auto* s : {leo.get(), meo.get(), geo.get()}) {
        const double near_r = s->calculate_regime_risk(cdm(1, 100, 5, 3600, 1e6));
        const double far_r = s->calculate_regime_risk(cdm(1, 50000, 5, 3600, 1e6));
        CHECK(near_r > far_r && near_r <= 1.0 && far_r >= 0.0);
    }
}

void test_evasion_directives() {
    auto geo = RegimeFactory::create_regime(35786);
    auto meo = RegimeFactory::create_regime(20200);
    auto leo = RegimeFactory::create_regime(550);

    // GEO, 3 days out: burn a quarter orbit before TCA, dv = disp * n
    const auto tg = cdm(7, 1000.0, 0.8, 3 * 86400.0, 3.0 * 1000.0 * 1000.0);
    const EvasionDirective g = geo->formulate_evasion_directive(tg);
    const double target = 5000.0;  // max(5 km floor, 2*1 km, 1.5 km)
    CHECK(g.plane == ManeuverPlane::CrossTrack && g.direction.n == 1.0 && g.regime == OrbitalRegime::GEO);
    CHECK_NEAR(g.lead_time_s, geo->orbital_period_s() / 4.0, 1e-6);
    CHECK_NEAR(g.delta_v_mps, std::sqrt(target * target - 1e6) * geo->mean_motion_rad_s(), 1e-9);
    CHECK_NEAR(g.predicted_separation_m, target, 1e-6);
    CHECK_NEAR(g.execute_no_later_than_s, 3 * 86400.0 - geo->orbital_period_s() / 4.0, 1e-6);
    CHECK(g.urgency == Urgency::Priority && g.within_regime_dv_cap);

    // MEO, half-orbit lead: dv = disp * n / 4
    const auto tm = cdm(8, 1000.0, 2.0, 2 * 86400.0, kNaN);
    const EvasionDirective m = meo->formulate_evasion_directive(tm);
    CHECK(m.plane == ManeuverPlane::Radial && m.direction.r == 1.0);
    CHECK_NEAR(m.lead_time_s, meo->orbital_period_s() / 2.0, 1e-6);
    CHECK_NEAR(m.delta_v_mps, std::sqrt(3000.0 * 3000.0 - 1e6) * meo->mean_motion_rad_s() / 4.0, 1e-9);

    // LEO, 3.3 days: 8-orbit lead, CW along-track gain
    const auto tl = cdm(9, 400.0, 12.0, 285000.0, 3.0 * 300.0 * 300.0);
    const EvasionDirective l = leo->formulate_evasion_directive(tl);
    const double n = leo->mean_motion_rad_s(), L = 8.0 * leo->orbital_period_s();
    CHECK(l.plane == ManeuverPlane::AlongTrack && l.direction.t == 1.0);
    CHECK_NEAR(l.lead_time_s, L, 1e-6);
    CHECK_NEAR(l.delta_v_mps, std::sqrt(1000.0 * 1000.0 - 400.0 * 400.0) / std::fabs(4.0 * std::sin(n * L) / n - 3.0 * L),
               1e-12);
    CHECK(l.urgency == Urgency::Routine && l.delta_v_mps < 0.01);

    // Urgency: inside LEO immediate window, and inside planning margin
    CHECK(leo->formulate_evasion_directive(cdm(9, 400, 12, 3 * 3600.0, 1e5)).urgency == Urgency::Immediate);
    const EvasionDirective now = leo->formulate_evasion_directive(cdm(9, 400, 12, 600.0, 1e5));
    CHECK(now.urgency == Urgency::Immediate && now.execute_no_later_than_s == 0.0 && now.lead_time_s == 600.0);
    // Regression: an implausible covariance (sigma ~ 1.1e8 m, seen in the ESA
    // set) must not drive the separation target. It is sized like a missing one.
    const EvasionDirective diluted = leo->formulate_evasion_directive(cdm(9, 3000.0, 12, 285000.0, 1.22042e16));
    CHECK_NEAR(diluted.target_separation_m, 4500.0, 1e-9);  // 1.5 * miss
    CHECK(diluted.within_regime_dv_cap);
    CHECK(!covariance_usable(1.22042e16) && !covariance_usable(kNaN) && !covariance_usable(0.0) && covariance_usable(1e6));
    // Very short lead needs large dv -> over the cap
    CHECK(!leo->formulate_evasion_directive(cdm(9, 10, 12, 60.0, 1e8)).within_regime_dv_cap);
}

// --------------------------------------------------------------------------
void test_memento_state_stack() {
    SpacecraftState s0{};
    s0.dv_budget_mps = 1.0;
    Spacecraft sc(s0);
    StateCaretaker ct;
    EvasionDirective d{};
    d.delta_v_mps = 0.25;

    for (std::uint32_t id = 1; id <= 3; ++id) {
        CHECK(ct.checkpoint(sc, id, 1));
        CHECK(sc.reserve(d));
    }
    CHECK_NEAR(sc.state().dv_budget_mps, 0.25, 1e-12);
    CHECK(ct.depth() == 3 && (sc.state().status_flags & status::kAdvisoryPending));

    CHECK(ct.rollback_last(sc));  // undo id 3
    CHECK_NEAR(sc.state().dv_budget_mps, 0.5, 1e-12);
    CHECK(sc.state().status_flags & status::kRollback);

    CHECK(ct.unwind_to(sc, 99) == 0 && ct.depth() == 2);  // unknown id: no change
    CHECK(ct.unwind_to(sc, 1) == 2 && ct.depth() == 0);   // undo 2 then 1
    CHECK_NEAR(sc.state().dv_budget_mps, 1.0, 1e-12);
    CHECK(sc.state().advisories_reserved == 0);

    EvasionDirective big{};
    big.delta_v_mps = 5.0;
    CHECK(!sc.reserve(big));  // over budget

    StateStack<int, 2> st;
    CHECK(st.push(1) && st.push(2) && !st.push(3) && st.rejected_count() == 1);
    int v = 0;
    CHECK(st.pop(v) && v == 2 && st.pop(v) && v == 1 && !st.pop(v));
}

void test_mission_ledger() {
    static MissionLedger L;  // large pool: static storage
    L.clear();
    LedgerRecord r{};
    std::vector<MissionLedger::Handle> hs;
    for (std::uint32_t i = 0; i < 5; ++i) {
        r.cycle = i;
        r.directive.alert_id = i;
        r.directive.delta_v_mps = 0.1 * (i + 1);
        hs.push_back(L.append(r));
    }
    CHECK(L.size() == 5);
    std::vector<std::uint32_t> fwd, bwd;
    L.traverse_forward([&](const LedgerRecord& x) { fwd.push_back(x.directive.alert_id); });
    L.traverse_backward([&](const LedgerRecord& x) { bwd.push_back(x.directive.alert_id); });
    CHECK((fwd == std::vector<std::uint32_t>{0, 1, 2, 3, 4}) && (bwd == std::vector<std::uint32_t>{4, 3, 2, 1, 0}));

    CHECK(L.set_status(hs[1], AdvisoryStatus::Committed) && L.set_status(hs[3], AdvisoryStatus::Committed));
    CHECK(L.set_status(hs[4], AdvisoryStatus::RolledBack));
    CHECK_NEAR(L.committed_dv_total(), 0.2 + 0.4, 1e-12);
    CHECK_NEAR(L.committed_dv_since_cycle(2), 0.4, 1e-12);

    // Fill past capacity: oldest evicted, stale handle detected
    for (std::size_t i = 0; i < MissionLedger::kCapacity; ++i) L.append(r);
    CHECK(L.size() == MissionLedger::kCapacity && L.evicted_count() == 5);
    CHECK(!L.set_status(hs[0], AdvisoryStatus::Committed) && L.get(hs[0]) == nullptr);
}

// --------------------------------------------------------------------------
void test_cdm_io() {
    ConjunctionTelemetry t{};
    CHECK(parse_cdm_csv_line("0,31816.0,7.929,591157.0,794423941.78,5.04700000000001e-08", t));
    CHECK(t.alert_id == 0 && t.miss_distance_m == 31816.0 && t.relative_speed_km_s == 7.929);
    CHECK(t.cov_trace_m2 == 794423941.78 && t.collision_prob == 5.04700000000001e-08);
    CHECK(parse_cdm_csv_line("276,1680.0,14.887,384405.3,,1e-30\r", t) && std::isnan(t.cov_trace_m2));
    CHECK(!parse_cdm_csv_line("x,1,2,3,4,0", t));
    CHECK(!parse_cdm_csv_line("1,1,2,3,4", t));       // missing field
    CHECK(!parse_cdm_csv_line("1,1,2,3,4,0,9", t));   // extra field
    CHECK(!parse_cdm_csv_line("1,,2,3,4,0", t));      // empty required field
    CHECK(!parse_cdm_csv_line("1,1,2,3,4,1.5", t));   // probability out of range
    CHECK(!parse_cdm_csv_line("1,nan,2,3,4,0", t));

    // Exact round trip, including missing covariance
    for (const auto& src : {cdm(42, 123.456789012345, 7.1, 86400.25, 1.0 / 3.0, 1e-7), cdm(43, 1, 2, 3, kNaN, 0)}) {
        char buf[256];
        const std::size_t n = format_cdm_csv_line(src, buf, sizeof buf);
        ConjunctionTelemetry back{};
        CHECK(n > 0 && parse_cdm_csv_line(buf, back));
        CHECK(back.alert_id == src.alert_id && back.miss_distance_m == src.miss_distance_m &&
              back.relative_speed_km_s == src.relative_speed_km_s && back.time_to_tca_sec == src.time_to_tca_sec &&
              back.collision_prob == src.collision_prob &&
              (back.cov_trace_m2 == src.cov_trace_m2 || (std::isnan(back.cov_trace_m2) && std::isnan(src.cov_trace_m2))));
        CHECK(format_cdm_json(src, buf, sizeof buf) > 0);
        CHECK(std::isnan(src.cov_trace_m2) == (std::strstr(buf, "\"cov_trace_m2\":null") != nullptr));
    }
    char tiny[8];
    CHECK(format_cdm_csv_line(cdm(1, 1, 1, 1, 1), tiny, sizeof tiny) == 0);

    // File load: header enforced, malformed rows counted
    const auto dir = std::filesystem::temp_directory_path() / "ocas_test_io";
    std::filesystem::create_directories(dir);
    const std::string good = (dir / "good.csv").string(), bad = (dir / "bad.csv").string();
    {
        std::vector<ConjunctionTelemetry> rows{cdm(1, 10, 1, 100, 5, 0.5), cdm(2, 20, 2, 200, kNaN, 0)};
        std::ofstream f(good);
        CHECK(write_cdm_csv(f, rows));
        f << "garbage line\n";
        std::ofstream b(bad);
        b << "id,miss\n1,2\n";
    }
    std::vector<ConjunctionTelemetry> rows;
    CdmLoadReport rep;
    CHECK(load_cdm_csv(good, rows, rep) && rep.loaded == 2 && rep.malformed == 1 && rep.missing_covariance == 1);
    rows.clear();
    CHECK(!load_cdm_csv(bad, rows, rep) && !rep.error.empty());
    CHECK(!load_cdm_csv((dir / "missing.csv").string(), rows, rep));
    std::filesystem::remove_all(dir);
}

// --------------------------------------------------------------------------
struct CountingObserver final : IPipelineObserver {
    int advisories = 0, resolved = 0, rollbacks = 0, rejected = 0;
    void on_advisory(const LedgerRecord&) override { ++advisories; }
    void on_advisory_resolved(const LedgerRecord&) override { ++resolved; }
    void on_rollback(std::uint32_t, std::size_t, const SpacecraftState&) override { ++rollbacks; }
    void on_input_rejected(const ConjunctionTelemetry&, InputRejectReason) override { ++rejected; }
};

void test_pipeline_integration() {
    PipelineConfig cfg;
    cfg.altitude_km = 550.0;
    cfg.dv_budget_mps = 1.0;
    std::string err;

    {
        static OcasPipeline p;
        CountingObserver obs;
        p.attach(&obs);
        CHECK(p.initialize(cfg, err));
        CHECK(p.regime().regime() == OrbitalRegime::LEO);
        p.ingest(cdm(100, 30.0, 12.0, 200000.0, 3.0 * 100.0 * 100.0));   // very close: actionable
        p.ingest(cdm(101, 60000.0, 12.0, 500000.0, 1e9));                // far: not actionable
        p.ingest(cdm(102, 200.0, 12.0, -5.0, 1e6));                      // past TCA: rejected
        p.ingest(cdm(103, 300.0, 12.0, 250000.0, kNaN));                  // fallback path, actionable
        CycleStats c = p.run_cycle();
        CHECK(c.ingested == 4 && c.rejected_input == 1 && c.scored == 3);
        CHECK(c.logistic_used == 2 && c.fallback_used == 1);
        CHECK(c.directives == 2 && c.committed == 2 && c.rolled_back == 0);
        CHECK(obs.rejected == 1 && obs.advisories == 2 && obs.resolved == 2);
        CHECK(p.spacecraft().state().dv_budget_mps < 1.0);
        CHECK_NEAR(p.ledger().committed_dv_total(), 1.0 - p.spacecraft().state().dv_budget_mps, 1e-12);

        // Same event updated next cycle: suppressed, no new directive
        p.ingest(cdm(100, 25.0, 12.0, 180000.0, 3.0 * 100.0 * 100.0));
        c = p.run_cycle();
        CHECK(c.directives == 0 && c.suppressed_duplicates == 1);

        // Overflow: 70 pushes into a 64-slot ring buffer before one cycle
        for (std::uint32_t i = 0; i < 70; ++i) p.ingest(cdm(1000 + i, 50000.0, 10.0, 4e5, 1e8));
        c = p.run_cycle();
        CHECK(c.ingested == 64 && c.ingest_overflow_total == 6);
    }
    {
        static OcasPipeline p;
        CountingObserver obs;
        p.attach(&obs);
        PipelineConfig c2 = cfg;
        c2.abort_every_n = 2;  // second staged advisory is aborted
        CHECK(p.initialize(c2, err));
        for (std::uint32_t i = 0; i < 3; ++i) p.ingest(cdm(200 + i, 20.0 + i, 12.0, 200000.0 + i, 3e4));
        const double before = p.spacecraft().state().dv_budget_mps;
        const CycleStats c = p.run_cycle();
        CHECK(c.directives == 3 && c.committed == 1 && c.rolled_back == 2);
        CHECK(obs.rollbacks == 1);
        // Only the first advisory's dv remains reserved
        double first_dv = 0.0;
        p.ledger().traverse_forward([&](const LedgerRecord& r) {
            if (r.status == AdvisoryStatus::Committed) first_dv = r.directive.delta_v_mps;
        });
        CHECK_NEAR(p.spacecraft().state().dv_budget_mps, before - first_dv, 1e-12);
        CHECK(p.caretaker().depth() == 0 && p.caretaker().rollbacks() == 2);
    }
    {
        static OcasPipeline p;
        PipelineConfig bad = cfg;
        bad.altitude_km = 20.0;
        CHECK(!p.initialize(bad, err) && !err.empty());
        bad = cfg;
        bad.max_advisories_per_cycle = 99;
        CHECK(!p.initialize(bad, err));
    }
    {
        // GEO host: logistic model is out of domain, everything uses the fallback
        static OcasPipeline p;
        PipelineConfig g = cfg;
        g.altitude_km = 35786.0;
        CHECK(p.initialize(g, err));
        p.ingest(cdm(300, 900.0, 0.6, 2 * 86400.0, 3.0 * 500.0 * 500.0));
        const CycleStats c = p.run_cycle();
        CHECK(c.fallback_used == 1 && c.logistic_used == 0 && c.directives == 1);
        bool cross_track = false;
        p.ledger().traverse_forward([&](const LedgerRecord& r) { cross_track = r.directive.plane == ManeuverPlane::CrossTrack; });
        CHECK(cross_track);
    }
}

void test_audit_logger() {
    const auto path = std::filesystem::temp_directory_path() / "ocas_test_log" / "audit.log";
    std::filesystem::remove_all(path.parent_path());
    {
        FlightAuditLogger log(path.string());
        CHECK(log.is_open());
        log.log("TEST", ",\"value\":%d", 7);
        LedgerRecord r{};
        log.on_advisory(r);
    }  // RAII close
    {
        FlightAuditLogger log(path.string());  // append, not truncate
        log.log("TEST2", "%s", "");
    }
    std::ifstream in(path);
    std::string line;
    int lines = 0;
    bool well_formed = true;
    while (std::getline(in, line)) {
        ++lines;
        if (line.front() != '{' || line.back() != '}' || line.find("\"event\":") == std::string::npos) well_formed = false;
    }
    CHECK(lines == 3 && well_formed);
    std::filesystem::remove_all(path.parent_path());
}

}  // namespace

int main() {
    struct {
        const char* name;
        void (*fn)();
    } const tests[] = {
        {"circular buffer", test_circular_buffer},
        {"max heap + threat comparator", test_max_heap},
        {"logistic regression strategy", test_logistic_strategy},
        {"conservative threshold strategy", test_conservative_strategy},
        {"risk classifier selection", test_risk_classifier_selection},
        {"regime factory", test_regime_factory},
        {"evasion directive physics", test_evasion_directives},
        {"memento state stack", test_memento_state_stack},
        {"mission ledger", test_mission_ledger},
        {"CDM CSV/JSON I/O", test_cdm_io},
        {"pipeline integration", test_pipeline_integration},
        {"audit logger", test_audit_logger},
    };
    for (const auto& t : tests) {
        const int before = g_failures;
        t.fn();
        std::printf("  [%s] %s\n", g_failures == before ? "PASS" : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
