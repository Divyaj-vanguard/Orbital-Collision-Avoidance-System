// OCAS verification suite. CHECK records a failure and continues.
#include "cdm_io.hpp"
#include "neural.hpp"
#include "pipeline.hpp"
#include "recorder.hpp"
#include "regime.hpp"
#include "risk.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
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

Regime regime_at(double altitude_km) {
    Regime r{};
    make_regime(altitude_km, r);
    return r;
}

// Hand-set LSTM whose output depends only on miss distance: one unit with
// input and output gates open, forget gate shut, and g = tanh(-2 * x0), where
// x0 = log10(d + 1) - 3. Score > 0.5 (actionable) exactly when d < ~1 km.
ml::LstmParams miss_distance_lstm() {
    using namespace ocas::ml;
    LstmParams p{};
    p.scaler.mean = {3.0, 0.0, 0.0, 0.0};
    p.b[0] = 30.0;                       // input gate open
    p.b[kLstmH] = -30.0;                 // forget gate shut: no memory between steps
    p.W[2 * kLstmH * kFeatures] = -2.0;  // candidate g from x0 only
    p.b[3 * kLstmH] = 30.0;              // output gate open
    p.w_out[0] = 4.0;
    p.threshold = 0.5;
    return p;
}

void test_threat_order() {
    ScoredThreat act{}, quiet{};
    act.risk.actionable = true;
    act.priority = 0.1;
    quiet.priority = 0.9;
    CHECK(ranks_below(quiet, act) && !ranks_below(act, quiet));  // actionable outranks higher priority
    ScoredThreat soon = act, late = act;
    soon.telemetry.time_to_tca_sec = 100.0;
    late.telemetry.time_to_tca_sec = 900.0;
    CHECK(ranks_below(late, soon));  // equal priority: earlier TCA ranks higher
    ScoredThreat older = soon, newer = soon;
    older.sequence = 1;
    newer.sequence = 2;
    CHECK(ranks_below(newer, older) && !ranks_below(older, newer));  // full tie: ingest order decides
}

void test_conservative_rule() {
    // Missing covariance -> geometry only, 5 km screen, continuous at boundary
    RiskAssessment a = conservative_assess(cdm(1, 4000.0, 7.0, 1e5, kNaN));
    CHECK(a.rule == ConservativeRule::GeometricOnly && a.actionable);
    a = conservative_assess(cdm(1, 6000.0, 7.0, 1e5, kNaN));
    CHECK(!a.actionable && a.score < kActionScore);
    CHECK_NEAR(conservative_assess(cdm(1, 4999.999, 7.0, 1e5, kNaN)).score, kActionScore, 1e-6);
    const double cov = 3.0 * 200.0 * 200.0;  // sigma = 200 m per axis
    const double expect = 400.0 / (2.0 * 40000.0) * std::exp(-(300.0 * 300.0) / (2.0 * 40000.0));
    CHECK_NEAR(estimate_pc(300.0, cov, 20.0), expect, 1e-15);
    a = conservative_assess(cdm(1, 1500.0, 7.0, 1e5, cov));
    CHECK(a.rule == ConservativeRule::PcEstimate && !a.actionable);  // Pc ~ 3.7e-14
    a = conservative_assess(cdm(1, 800.0, 7.0, 1e5, 1e10));
    CHECK(a.rule == ConservativeRule::ScreeningVolume && a.actionable && a.score >= 0.6);
    // Dilution: implausible covariance falls back to geometry
    CHECK(conservative_assess(cdm(1, 2000.0, 7.0, 1e5, 1e14)).rule == ConservativeRule::GeometricOnly);
}

void test_model_selection() {
    CHECK(select_model(cdm(1, 1000, 10, 2e5, 1e6), true) == ModelSelection::InDomain);
    CHECK(select_model(cdm(1, 1000, 10, 2e5, kNaN), true) == ModelSelection::CovarianceMissing);
    CHECK(select_model(cdm(1, 1000, 10, 2e5, 0.0), true) == ModelSelection::CovarianceMissing);
    CHECK(select_model(cdm(1, 1000, 10, 2e5, 1e14), true) == ModelSelection::CovarianceImplausible);
    CHECK(select_model(cdm(1, 1000, 20, 2e5, 1e6), true) == ModelSelection::VelocityOutOfEnvelope);
    CHECK(select_model(cdm(1, 1000, 1, 2e5, 1e6), false) == ModelSelection::RegimeOutOfDomain);
    static LstmModel lstm(miss_distance_lstm());
    const RiskAssessment a = assess_risk(lstm, cdm(1, 1000, 10, 2e5, kNaN), true);
    CHECK(a.model == RiskModel::ConservativeThreshold && a.selection == ModelSelection::CovarianceMissing);
    CHECK(assess_risk(lstm, cdm(1, 1000, 10, 2e5, 1e6), true).model == RiskModel::Lstm);
    CHECK(assess_risk(lstm, cdm(2, 100, 10, 2e5, 1e6), true).actionable);
    CHECK(!assess_risk(lstm, cdm(3, 50000, 10, 2e5, 1e6), true).actionable);
}

void test_regimes() {
    OrbitalRegime r{};
    CHECK(classify_regime(550, r) && r == OrbitalRegime::LEO);
    CHECK(classify_regime(1999.9, r) && r == OrbitalRegime::LEO);
    CHECK(classify_regime(2000, r) && r == OrbitalRegime::MEO);
    CHECK(classify_regime(20200, r) && r == OrbitalRegime::MEO);
    CHECK(classify_regime(35586, r) && r == OrbitalRegime::GEO);
    CHECK(classify_regime(35786, r) && r == OrbitalRegime::GEO);
    CHECK(!classify_regime(50, r) && !classify_regime(kNaN, r));
    Regime bad{};
    CHECK(!make_regime(-1, bad));

    const Regime leo = regime_at(550), meo = regime_at(20200), geo = regime_at(35786);
    CHECK(leo.kind == OrbitalRegime::LEO && meo.kind == OrbitalRegime::MEO && geo.kind == OrbitalRegime::GEO);
    CHECK_NEAR(geo.period_s(), 86164.1, 1.0);  // sidereal day
    CHECK_NEAR(leo.period_s(), 5739.0, 5.0);   // ~95.7 min at 550 km
    for (const Regime* s : {&leo, &meo, &geo}) {
        const double near_r = regime_risk(*s, cdm(1, 100, 5, 3600, 1e6));
        const double far_r = regime_risk(*s, cdm(1, 50000, 5, 3600, 1e6));
        CHECK(near_r > far_r && near_r <= 1.0 && far_r >= 0.0);
    }
}

void test_evasion_directives() {
    const Regime geo = regime_at(35786), meo = regime_at(20200), leo = regime_at(550);

    // GEO, 3 days out: burn a quarter orbit before TCA, dv = disp * n
    const EvasionDirective g = plan_evasion(geo, cdm(7, 1000.0, 0.8, 3 * 86400.0, 3.0 * 1000.0 * 1000.0));
    const double target = 5000.0;  // max(5 km floor, 2*1 km, 1.5 km)
    CHECK(g.plane == ManeuverPlane::CrossTrack && g.direction.n == 1.0 && g.regime == OrbitalRegime::GEO);
    CHECK_NEAR(g.lead_time_s, geo.period_s() / 4.0, 1e-6);
    CHECK_NEAR(g.delta_v_mps, std::sqrt(target * target - 1e6) * geo.n, 1e-9);
    CHECK_NEAR(g.predicted_separation_m, target, 1e-6);
    CHECK_NEAR(g.execute_no_later_than_s, 3 * 86400.0 - geo.period_s() / 4.0, 1e-6);
    CHECK(g.urgency == Urgency::Priority && g.within_regime_dv_cap);

    // MEO, half-orbit lead: dv = disp * n / 4
    const EvasionDirective m = plan_evasion(meo, cdm(8, 1000.0, 2.0, 2 * 86400.0, kNaN));
    CHECK(m.plane == ManeuverPlane::Radial && m.direction.r == 1.0);
    CHECK_NEAR(m.lead_time_s, meo.period_s() / 2.0, 1e-6);
    CHECK_NEAR(m.delta_v_mps, std::sqrt(3000.0 * 3000.0 - 1e6) * meo.n / 4.0, 1e-9);

    // LEO, 3.3 days: 8-orbit lead, CW along-track gain
    const EvasionDirective l = plan_evasion(leo, cdm(9, 400.0, 12.0, 285000.0, 3.0 * 300.0 * 300.0));
    const double n = leo.n, L = 8.0 * leo.period_s();
    CHECK(l.plane == ManeuverPlane::AlongTrack && l.direction.t == 1.0);
    CHECK_NEAR(l.lead_time_s, L, 1e-6);
    CHECK_NEAR(l.delta_v_mps, std::sqrt(1000.0 * 1000.0 - 400.0 * 400.0) / std::fabs(4.0 * std::sin(n * L) / n - 3.0 * L),
               1e-12);
    CHECK(l.urgency == Urgency::Routine && l.delta_v_mps < 0.01);

    // Urgency: inside LEO immediate window, and inside planning margin
    CHECK(plan_evasion(leo, cdm(9, 400, 12, 3 * 3600.0, 1e5)).urgency == Urgency::Immediate);
    const EvasionDirective now = plan_evasion(leo, cdm(9, 400, 12, 600.0, 1e5));
    CHECK(now.urgency == Urgency::Immediate && now.execute_no_later_than_s == 0.0 && now.lead_time_s == 600.0);
    // Regression: an implausible covariance (sigma ~ 1.1e8 m, seen in the ESA
    // set) must not drive the separation target. It is sized like a missing one.
    const EvasionDirective diluted = plan_evasion(leo, cdm(9, 3000.0, 12, 285000.0, 1.22042e16));
    CHECK_NEAR(diluted.target_separation_m, 4500.0, 1e-9);  // 1.5 * miss
    CHECK(diluted.within_regime_dv_cap);
    CHECK(!covariance_usable(1.22042e16) && !covariance_usable(kNaN) && !covariance_usable(0.0) && covariance_usable(1e6));
    // Very short lead needs large dv -> over the cap
    CHECK(!plan_evasion(leo, cdm(9, 10, 12, 60.0, 1e8)).within_regime_dv_cap);
}

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

void test_pipeline() {
    PipelineConfig cfg;
    cfg.altitude_km = 550.0;
    cfg.dv_budget_mps = 1.0;
    cfg.lstm = miss_distance_lstm();
    std::string err;
    {
        static Pipeline p;
        CHECK(p.initialize(cfg, nullptr, err));
        CHECK(p.regime().kind == OrbitalRegime::LEO);
        p.ingest(cdm(100, 30.0, 12.0, 200000.0, 3.0 * 100.0 * 100.0));  // very close: actionable
        p.ingest(cdm(101, 60000.0, 12.0, 500000.0, 1e9));               // far: not actionable
        p.ingest(cdm(102, 200.0, 12.0, -5.0, 1e6));                     // past TCA: rejected
        p.ingest(cdm(103, 300.0, 12.0, 250000.0, kNaN));                 // fallback path, actionable
        CycleStats c = p.run_cycle();
        CHECK(c.ingested == 4 && c.rejected_input == 1 && c.scored == 3);
        CHECK(c.model_used == 2 && c.fallback_used == 1);
        CHECK(c.directives == 2 && c.committed == 2 && c.rolled_back == 0);
        const double expected_dv = plan_evasion(p.regime(), cdm(100, 30.0, 12.0, 200000.0, 3e4)).delta_v_mps +
                                   plan_evasion(p.regime(), cdm(103, 300.0, 12.0, 250000.0, kNaN)).delta_v_mps;
        CHECK_NEAR(p.dv_budget_mps(), 1.0 - expected_dv, 1e-12);

        // Same event updated next cycle: suppressed, no new directive
        p.ingest(cdm(100, 25.0, 12.0, 180000.0, 3.0 * 100.0 * 100.0));
        c = p.run_cycle();
        CHECK(c.directives == 0 && c.suppressed_duplicates == 1);

        // Overflow: 70 pushes into a 64-slot inbox before one cycle; the oldest 6 are dropped
        std::size_t dropped = 0;
        for (std::uint32_t i = 0; i < 70; ++i) dropped += p.ingest(cdm(1000 + i, 50000.0, 10.0, 4e5, 1e8));
        c = p.run_cycle();
        CHECK(dropped == 6 && c.ingested == 64 && c.ingest_overflow_total == 6);
    }
    {
        static Pipeline p;
        PipelineConfig c2 = cfg;
        c2.abort_every_n = 2;  // second staged advisory is aborted, which also undoes the third
        CHECK(p.initialize(c2, nullptr, err));
        for (std::uint32_t i = 0; i < 3; ++i) p.ingest(cdm(200 + i, 20.0 + i, 12.0, 200000.0 + i, 3e4));
        const CycleStats c = p.run_cycle();
        CHECK(c.directives == 3 && c.committed == 1 && c.rolled_back == 2);
        // Heap order puts the earliest TCA (alert 200) first; only its dv stays reserved
        const double first_dv = plan_evasion(p.regime(), cdm(200, 20.0, 12.0, 200000.0, 3e4)).delta_v_mps;
        CHECK_NEAR(p.dv_budget_mps(), 1.0 - first_dv, 1e-12);
    }
    {
        static Pipeline p;
        PipelineConfig bad = cfg;
        bad.altitude_km = 20.0;
        CHECK(!p.initialize(bad, nullptr, err) && !err.empty());
        bad = cfg;
        bad.max_advisories_per_cycle = 99;
        CHECK(!p.initialize(bad, nullptr, err));
    }
    {
        // GEO host: the LSTM is out of domain, everything uses the fallback
        static Pipeline p;
        PipelineConfig g = cfg;
        g.altitude_km = 35786.0;
        CHECK(p.initialize(g, nullptr, err));
        p.ingest(cdm(300, 900.0, 0.6, 2 * 86400.0, 3.0 * 500.0 * 500.0));
        const CycleStats c = p.run_cycle();
        CHECK(c.fallback_used == 1 && c.model_used == 0 && c.directives == 1 && c.committed == 1);
    }
}

void test_recorder() {
    const auto path = std::filesystem::temp_directory_path() / "ocas_test_log" / "audit.log";
    std::filesystem::remove_all(path.parent_path());
    LedgerRecord r{};
    {
        Recorder rec(path.string(), false, true, false);
        CHECK(rec.is_open());
        rec.advisory(r);
        rec.advisory_resolved(r);
    }  // RAII close
    {
        Recorder rec(path.string(), false, true, false);  // append, not truncate
        rec.rollback(1, 2, 0.5);
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

void test_ml() {
    using namespace ocas::ml;
    const Features raw = raw_features(cdm(1, 999.0, 7.5, 2 * 86400.0, 99.0 * 99.0));
    CHECK_NEAR(raw[0], 3.0, 1e-12);
    CHECK_NEAR(raw[1], 7.5, 1e-12);
    CHECK_NEAR(raw[2], 2.0, 1e-12);
    CHECK_NEAR(raw[3], 2.0, 1e-12);
    FeatureScaler sc;
    sc.mean = {1, 0, 0, 0};
    sc.stddev = {2, 1, 1, 1};
    CHECK_NEAR(standardize(raw, sc)[0], 1.0, 1e-12);

    // LSTM with zero weights except g-gate bias atanh(0.5): i = f = o = 0.5, g = 0.5
    LstmParams l{};
    for (std::size_t j = 0; j < kLstmH; ++j) l.b[2 * kLstmH + j] = std::atanh(0.5);
    l.w_out[0] = 1.0;
    LstmState st{};
    CHECK_NEAR(lstm_step(l, {0, 0, 0, 0}, st), 0.5 * std::tanh(0.25), 1e-12);   // c = 0.25
    CHECK_NEAR(lstm_step(l, {0, 0, 0, 0}, st), 0.5 * std::tanh(0.375), 1e-12);  // c = 0.5*0.25 + 0.25

    // Weight file round trip; wrong architecture rejected
    const auto dir = std::filesystem::temp_directory_path() / "ocas_test_ml";
    std::filesystem::create_directories(dir);
    const std::string lp = (dir / "lstm.txt").string(), bad = (dir / "other.txt").string();
    std::ofstream(bad) << "arch some_other_model\n";
    std::string err;
    LstmParams w{};
    w.U[5] = 1.0 / 3.0;
    w.threshold = 0.7;
    CHECK(save_lstm(lp, w, "t"));
    LstmParams w2{};
    CHECK(load_lstm(lp, w2, err) && w2.U == w.U && w2.threshold == 0.7);
    CHECK(!load_lstm(bad, w2, err) && !err.empty());
    std::filesystem::remove_all(dir);

    // The model keeps independent state per event
    l.b[2 * kLstmH] = std::atanh(0.5);
    static LstmModel m(l);
    const auto a1 = m.assess(cdm(1, 500, 10, 3e5, 1e6));
    const auto b1 = m.assess(cdm(2, 500, 10, 3e5, 1e6));
    const auto a2 = m.assess(cdm(1, 500, 10, 3e5, 1e6));
    CHECK(a1.model == RiskModel::Lstm && a1.score == b1.score && m.tracked_events() == 2);
    CHECK(a2.logit != a1.logit);  // second step of event 1 sees its own history
    // A new event in a full probe window evicts the least recently used one
    static LstmModel small(l);
    for (std::uint32_t id = 0; id < 3 * LstmModel::kEvents; ++id) small.assess(cdm(id, 500, 10, 3e5, 1e6));
    CHECK(small.tracked_events() == LstmModel::kEvents);

#ifdef OCAS_SOURCE_DIR
    LstmParams shipped{};
    CHECK(load_lstm(std::string(OCAS_SOURCE_DIR) + "/ml/models/lstm_weights.txt", shipped, err));
    CHECK(shipped.threshold > 0.0 && shipped.threshold < 1.0);
#endif
}

}  // namespace

int main() {
    struct {
        const char* name;
        void (*fn)();
    } const tests[] = {
        {"threat ordering", test_threat_order},
        {"conservative rule", test_conservative_rule},
        {"model selection", test_model_selection},
        {"regimes", test_regimes},
        {"evasion directive physics", test_evasion_directives},
        {"CDM CSV I/O", test_cdm_io},
        {"pipeline", test_pipeline},
        {"recorder", test_recorder},
        {"ML features, LSTM, weight files", test_ml},
    };
    for (const auto& t : tests) {
        const int before = g_failures;
        t.fn();
        std::printf("  [%s] %s\n", g_failures == before ? "PASS" : "FAIL", t.name);
    }
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
