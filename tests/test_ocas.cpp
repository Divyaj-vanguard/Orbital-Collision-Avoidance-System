// OCAS self-test. Prints each failed check and exits 1 if any failed.
#include "cdm_io.hpp"
#include "pipeline.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace ocas;

namespace {

int failures = 0;
#define CHECK(cond) \
    if (!(cond)) ++failures, std::printf("FAIL line %d: %s\n", __LINE__, #cond)

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

ConjunctionTelemetry cdm(std::uint32_t id, double d, double v, double t, double cov, double pc = 0.0) {
    return {id, d, v, t, cov, pc};
}

Regime regime_at(double altitude_km) {
    Regime r{};
    make_regime(altitude_km, r);
    return r;
}

// One-unit LSTM with no memory between steps: actionable exactly when miss distance < ~1 km.
ml::LstmParams miss_distance_lstm() {
    ml::LstmParams p{};
    p.scaler.mean = {3.0, 0.0, 0.0, 0.0};
    p.b[0] = 30.0;                               // input gate open
    p.b[ml::kLstmH] = -30.0;                     // forget gate shut
    p.W[2 * ml::kLstmH * ml::kFeatures] = -2.0;  // candidate from log miss distance only
    p.b[3 * ml::kLstmH] = 30.0;                  // output gate open
    p.w_out[0] = 4.0;
    return p;
}

void test_risk() {
    ScoredThreat act{}, quiet{};
    act.risk.actionable = true;
    act.priority = 0.1;
    quiet.priority = 0.9;
    CHECK(ranks_below(quiet, act));  // actionable outranks a higher priority

    CHECK(conservative_assess(cdm(1, 4000, 7, 1e5, kNaN)).actionable);  // no covariance: 5 km geometric screen
    CHECK(!conservative_assess(cdm(1, 6000, 7, 1e5, kNaN)).actionable);
    CHECK(conservative_assess(cdm(1, 2000, 7, 1e5, 1e14)).rule == ConservativeRule::GeometricOnly);  // diluted
    CHECK(conservative_assess(cdm(1, 800, 7, 1e5, 1e10)).actionable);  // inside 1 km: always act
    const double cov = 3.0 * 200.0 * 200.0;
    CHECK(near(estimate_pc(300, cov, 20), 400.0 / 80000.0 * std::exp(-90000.0 / 80000.0), 1e-15));

    CHECK(select_model(cdm(1, 1000, 10, 2e5, kNaN), true) == ModelSelection::CovarianceMissing);
    CHECK(select_model(cdm(1, 1000, 20, 2e5, 1e6), true) == ModelSelection::VelocityOutOfEnvelope);
    CHECK(select_model(cdm(1, 1000, 10, 2e5, 1e6), false) == ModelSelection::RegimeOutOfDomain);
    static LstmModel lstm(miss_distance_lstm());
    CHECK(assess_risk(lstm, cdm(2, 100, 10, 2e5, 1e6), true).actionable);
    CHECK(!assess_risk(lstm, cdm(3, 50000, 10, 2e5, 1e6), true).actionable);
    CHECK(assess_risk(lstm, cdm(4, 100, 10, 2e5, kNaN), true).model == RiskModel::ConservativeThreshold);
}

void test_physics() {
    OrbitalRegime r{};
    CHECK(classify_regime(2000, r) && r == OrbitalRegime::MEO);
    CHECK(classify_regime(35586, r) && r == OrbitalRegime::GEO);
    CHECK(!classify_regime(50, r));

    const Regime leo = regime_at(550), meo = regime_at(20200), geo = regime_at(35786);
    CHECK(near(geo.period_s(), 86164.1, 1.0));  // sidereal day

    // Expected dv = sqrt(target^2 - d^2) / CW gain at the regime's preferred lead time.
    const EvasionDirective g = plan_evasion(geo, cdm(7, 1000, 0.8, 3 * 86400.0, 3e6));
    CHECK(g.plane == ManeuverPlane::CrossTrack && near(g.delta_v_mps, std::sqrt(5000.0 * 5000 - 1e6) * geo.n));
    const EvasionDirective m = plan_evasion(meo, cdm(8, 1000, 2, 2 * 86400.0, kNaN));
    CHECK(near(m.delta_v_mps, std::sqrt(3000.0 * 3000 - 1e6) * meo.n / 4.0));
    const double n = leo.n, L = 8.0 * leo.period_s();
    const EvasionDirective l = plan_evasion(leo, cdm(9, 400, 12, 285000, 2.7e5));
    CHECK(near(l.delta_v_mps, std::sqrt(1e6 - 1.6e5) / std::fabs(4.0 * std::sin(n * L) / n - 3.0 * L), 1e-12));

    CHECK(plan_evasion(leo, cdm(9, 400, 12, 600, 1e5)).urgency == Urgency::Immediate);
    CHECK(!plan_evasion(leo, cdm(9, 10, 12, 60, 1e8)).within_regime_dv_cap);
    // Regression: a diluted covariance must not drive the target; it is sized like a missing one.
    CHECK(near(plan_evasion(leo, cdm(9, 3000, 12, 285000, 1.22042e16)).target_separation_m, 4500.0));
}

void test_cdm_io() {
    ConjunctionTelemetry t{};
    CHECK(parse_cdm_csv_line("276,1680.0,14.887,384405.3,,1e-30\r", t) && std::isnan(t.cov_trace_m2));
    CHECK(!parse_cdm_csv_line("1,1,2,3,4,0,9", t));  // extra field
    CHECK(!parse_cdm_csv_line("1,,2,3,4,0", t));     // empty required field
    CHECK(!parse_cdm_csv_line("1,1,2,3,4,1.5", t));  // probability out of range

    const auto src = cdm(42, 123.456789012345, 7.1, 86400.25, 1.0 / 3.0, 1e-7);
    char buf[256];
    ConjunctionTelemetry back{};
    CHECK(format_cdm_csv_line(src, buf, sizeof buf) > 0 && parse_cdm_csv_line(buf, back));
    CHECK(back.miss_distance_m == src.miss_distance_m && back.cov_trace_m2 == src.cov_trace_m2);  // exact

    const std::string path = (std::filesystem::temp_directory_path() / "ocas_test.csv").string();
    {
        std::ofstream f(path);
        write_cdm_csv(f, {cdm(1, 10, 1, 100, 5, 0.5), cdm(2, 20, 2, 200, kNaN, 0)});
        f << "garbage line\n";
    }
    std::vector<ConjunctionTelemetry> rows;
    CdmLoadReport rep;
    CHECK(load_cdm_csv(path, rows, rep) && rep.loaded == 2 && rep.malformed == 1);
    std::ofstream(path) << "id,miss\n1,2\n";
    CHECK(!load_cdm_csv(path, rows, rep));  // wrong header
    std::filesystem::remove(path);
}

void test_pipeline() {
    PipelineConfig cfg;
    cfg.dv_budget_mps = 1.0;
    cfg.lstm = miss_distance_lstm();
    std::string err;

    static Pipeline p;
    CHECK(p.initialize(cfg, nullptr, err));
    p.ingest(cdm(100, 30, 12, 200000, 3e4));   // close: advised
    p.ingest(cdm(101, 60000, 12, 500000, 1e9));  // far: ignored
    p.ingest(cdm(102, 200, 12, -5, 1e6));        // past TCA: rejected
    CycleStats c = p.run_cycle();
    CHECK(c.rejected_input == 1 && c.directives == 1 && c.committed == 1);
    CHECK(near(p.dv_budget_mps(), 1.0 - plan_evasion(p.regime(), cdm(100, 30, 12, 200000, 3e4)).delta_v_mps, 1e-12));
    p.ingest(cdm(100, 25, 12, 180000, 3e4));
    CHECK(p.run_cycle().suppressed_duplicates == 1);  // same event already handled
    std::size_t dropped = 0;
    for (std::uint32_t i = 0; i < 70; ++i) dropped += p.ingest(cdm(1000 + i, 50000, 10, 4e5, 1e8));
    CHECK(dropped == 6 && p.run_cycle().ingested == 64);  // inbox holds 64

    static Pipeline q;
    cfg.abort_every_n = 2;  // aborting the 2nd advisory also undoes the 3rd
    CHECK(q.initialize(cfg, nullptr, err));
    for (std::uint32_t i = 0; i < 3; ++i) q.ingest(cdm(200 + i, 20.0 + i, 12, 200000.0 + i, 3e4));
    c = q.run_cycle();
    CHECK(c.committed == 1 && c.rolled_back == 2);
    CHECK(near(q.dv_budget_mps(), 1.0 - plan_evasion(q.regime(), cdm(200, 20, 12, 200000, 3e4)).delta_v_mps, 1e-12));

    cfg.altitude_km = 20.0;
    CHECK(!q.initialize(cfg, nullptr, err));  // sub-orbital
}

void test_recorder() {
    const std::string path = (std::filesystem::temp_directory_path() / "ocas_test_audit.log").string();
    std::filesystem::remove(path);
    { Recorder(path, false, true, false).advisory(LedgerRecord{}); }
    { Recorder(path, false, true, false).rollback(1, 2, 0.5); }  // appends, never truncates
    std::ifstream in(path);
    int lines = 0;
    for (std::string line; std::getline(in, line); ++lines) CHECK(line.front() == '{' && line.back() == '}');
    CHECK(lines == 2);
    std::filesystem::remove(path);
}

void test_lstm() {
    ml::LstmParams l{};  // zero weights except the g-gate bias: i = f = o = 0.5, g = 0.5
    for (std::size_t j = 0; j < ml::kLstmH; ++j) l.b[2 * ml::kLstmH + j] = std::atanh(0.5);
    l.w_out[0] = 1.0;
    ml::LstmState st{};
    CHECK(near(ml::lstm_step(l, {}, st), 0.5 * std::tanh(0.25), 1e-12));
    CHECK(near(ml::lstm_step(l, {}, st), 0.5 * std::tanh(0.375), 1e-12));  // remembers c

    static LstmModel m(l);
    const double first = m.assess(cdm(1, 500, 10, 3e5, 1e6)).logit;
    CHECK(m.assess(cdm(2, 500, 10, 3e5, 1e6)).logit == first);  // new event starts fresh
    CHECK(m.assess(cdm(1, 500, 10, 3e5, 1e6)).logit != first);  // event 1 sees its history

    const std::string path = (std::filesystem::temp_directory_path() / "ocas_test_lstm.txt").string();
    ml::LstmParams back{};
    std::string err;
    CHECK(ml::save_lstm(path, l, "test") && ml::load_lstm(path, back, err) && back.b == l.b);
    std::filesystem::remove(path);
#ifdef OCAS_SOURCE_DIR
    CHECK(ml::load_lstm(std::string(OCAS_SOURCE_DIR) + "/ml/models/lstm_weights.txt", back, err));
#endif
}

}  // namespace

int main() {
    test_risk();
    test_physics();
    test_cdm_io();
    test_pipeline();
    test_recorder();
    test_lstm();
    std::printf("%s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}
