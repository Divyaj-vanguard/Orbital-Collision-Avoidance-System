// ============================================================================
// OCAS: Autonomous Orbital Collision Avoidance System
// Flight executive / CLI runner.
// Team DSCPP-III-2026-T018 | Graphic Era University
// ============================================================================
// Modes
//   (default)    stream CDM test vectors through the cyclic pipeline
//   --evaluate   offline validation of the risk strategies against the
//                provider Pc label (collision_prob > 1e-4)
// ============================================================================
#include "core/alloc_tracker.hpp"
#include "core/cdm_io.hpp"
#include "pipeline/ocas_pipeline.hpp"
#include "persistence/flight_audit_logger.hpp"
#include "telemetry/terminal_telemetry.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    std::string input = "data/esa/cleaned_cdm_dataset.csv";
    std::string log = "logs/flight_audit.log";
    std::string lstm_weights = "ml/models/lstm_weights.txt";
    double altitude_km = 550.0;
    double dv_budget_mps = 10.0;
    std::size_t batch = 32;
    std::size_t max_cdms = 0;
    std::size_t max_advisories = 8;
    std::uint32_t abort_every = 0;
    std::uint64_t summary_every = 100;  ///< --verbose only
    bool quiet = false;
    bool verbose = false;
    bool color = true;
    bool evaluate = false;
    bool log_all_threats = false;
};

void usage(const char* argv0) {
    std::printf(
        "Usage: %s [options]\n"
        "  --input PATH          CDM CSV (6-field schema)      [data/esa/cleaned_cdm_dataset.csv]\n"
        "  --altitude-km X       host altitude, selects regime [550]\n"
        "  --batch N             CDMs ingested per cycle       [32]  (>64 forces ring-buffer overwrite)\n"
        "  --max-cdms N          stop after N rows             [all]\n"
        "  --max-advisories N    directives per cycle, 1..16   [8]\n"
        "  --abort-every N       simulate OEM abort of every Nth staged advisory [0 = off]\n"
        "  --dv-budget X         collision-avoidance dv budget [10 m/s]\n"
        "  --lstm-weights PATH   LSTM weights file             [ml/models/lstm_weights.txt]\n"
        "  --log PATH            append-only audit log         [logs/flight_audit.log]\n"
        "  --log-all-threats     audit every scored CDM, not only actionable ones\n"
        "  --quiet               summary only, no manoeuvre table\n"
        "  --verbose             every manoeuvre plus latency, memory and ledger detail\n"
        "  --summary-every N     with --verbose: cycle summary period, 0 = off [100]\n"
        "  --no-color            disable ANSI colour\n"
        "  --evaluate            offline validation against collision_prob > 1e-4\n",
        argv0);
}

bool parse_number(const char* s, double& out) {
    char* end = nullptr;
    out = std::strtod(s, &end);
    return end != s && *end == '\0';
}

bool parse_count(const char* s, std::size_t& out) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0' || s[0] == '-') return false;
    out = static_cast<std::size_t>(v);
    return true;
}

bool parse_cli(int argc, char** argv, CliOptions& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char*& v) {
            if (i + 1 >= argc) return false;
            v = argv[++i];
            return true;
        };
        const char* v = nullptr;
        std::size_t n = 0;
        if (a == "--help" || a == "-h") { usage(argv[0]); std::exit(0); }
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--verbose") o.verbose = true;
        else if (a == "--no-color") o.color = false;
        else if (a == "--evaluate") o.evaluate = true;
        else if (a == "--log-all-threats") o.log_all_threats = true;
        else if (a == "--input" && need(v)) o.input = v;
        else if (a == "--lstm-weights" && need(v)) o.lstm_weights = v;
        else if (a == "--log" && need(v)) o.log = v;
        else if (a == "--altitude-km" && need(v) && parse_number(v, o.altitude_km)) {}
        else if (a == "--dv-budget" && need(v) && parse_number(v, o.dv_budget_mps)) {}
        else if (a == "--batch" && need(v) && parse_count(v, o.batch) && o.batch > 0) {}
        else if (a == "--max-cdms" && need(v) && parse_count(v, o.max_cdms)) {}
        else if (a == "--max-advisories" && need(v) && parse_count(v, o.max_advisories)) {}
        else if (a == "--summary-every" && need(v) && parse_count(v, n)) o.summary_every = n;
        else if (a == "--abort-every" && need(v) && parse_count(v, n)) o.abort_every = static_cast<std::uint32_t>(n);
        else {
            std::fprintf(stderr, "error: bad or incomplete option '%s'\n", a.c_str());
            return false;
        }
    }
    return true;
}

/// 24484 -> "24,484". Returns a pointer into one of two rotating buffers.
const char* commas(std::size_t v) {
    static char buf[2][32];
    static int k = 0;
    char* out = buf[k ^= 1];
    char tmp[32];
    const int n = std::snprintf(tmp, sizeof tmp, "%zu", v);
    int j = 0;
    for (int i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out[j++] = ',';
        out[j++] = tmp[i];
    }
    out[j] = '\0';
    return out;
}

// ---------------------------------------------------------------------------
// Offline evaluation
// ---------------------------------------------------------------------------
struct Confusion {
    std::size_t tp = 0, fp = 0, tn = 0, fn = 0;
    void add(bool predicted, bool actual) {
        if (predicted && actual) ++tp;
        else if (predicted) ++fp;
        else if (actual) ++fn;
        else ++tn;
    }
    [[nodiscard]] double precision() const { return tp + fp == 0 ? 0.0 : double(tp) / double(tp + fp); }
    [[nodiscard]] double recall() const { return tp + fn == 0 ? 0.0 : double(tp) / double(tp + fn); }
    [[nodiscard]] double specificity() const { return tn + fp == 0 ? 0.0 : double(tn) / double(tn + fp); }
    [[nodiscard]] double accuracy() const {
        const std::size_t n = tp + fp + tn + fn;
        return n == 0 ? 0.0 : double(tp + tn) / double(n);
    }
    [[nodiscard]] double f1() const {
        const double p = precision(), r = recall();
        return p + r == 0.0 ? 0.0 : 2.0 * p * r / (p + r);
    }
};

int run_evaluation(const std::vector<ocas::ConjunctionTelemetry>& rows, const ocas::RiskClassifier& clf,
                   bool verbose, const ocas::Ansi& a) {
    constexpr double kLabelPc = 1.0e-4;  // same label definition as tools/clean_esa_dataset.cpp
    Confusion lstm, conservative, hybrid, baseline;
    std::size_t by_sel[5] = {0, 0, 0, 0, 0};
    std::size_t positives = 0;
    for (const auto& t : rows) {
        if (!(t.time_to_tca_sec > 0.0) || !(t.miss_distance_m >= 0.0)) continue;
        const bool label = t.collision_prob > kLabelPc;
        positives += label ? 1 : 0;
        const ocas::ModelSelection sel = clf.select(t);
        ++by_sel[static_cast<int>(sel)];
        // Each strategy is evaluated exactly once per row: the LSTM is stateful.
        const bool p_lstm = clf.lstm().assess(t).actionable;
        const bool p_con = clf.conservative().assess(t).actionable;
        lstm.add(p_lstm, label);
        conservative.add(p_con, label);
        hybrid.add(sel == ocas::ModelSelection::InDomain ? p_lstm : p_con, label);
        baseline.add(t.miss_distance_m < 1000.0, label);
    }
    if (!verbose) {
        std::printf("%sMODEL CHECK%s  %s real ESA alerts, %s truly dangerous (provider Pc > 1e-4)\n\n", a.bold,
                    a.reset, commas(rows.size()), commas(positives));
        std::printf("  %-28s %9s %12s %12s %10s\n", "", "accuracy", "sensitivity", "specificity", "precision");
        auto line = [](const char* name, const Confusion& c) {
            std::printf("  %-28s %9.3f %12.3f %12.3f %10.3f\n", name, c.accuracy(), c.recall(), c.specificity(),
                        c.precision());
        };
        line("Simple rule: miss < 1 km", baseline);
        line("LSTM", lstm);
        line("Safety rule", conservative);
        line("OCAS (LSTM + safety rule)", hybrid);
        std::printf("\n  sensitivity = dangerous alerts caught | specificity = safe alerts correctly left alone\n");
        return 0;
    }
    std::printf("%s%sOFFLINE EVALUATION%s  label: collision_prob > 1e-4 (per CDM, %zu rows, %zu positive)\n",
                a.bold, a.cyan, a.reset, rows.size(), positives);
    std::printf("  model selection: in-domain %zu | cov missing %zu | cov implausible %zu | vel out %zu | "
                "regime out %zu\n\n",
                by_sel[0], by_sel[1], by_sel[2], by_sel[3], by_sel[4]);
    std::printf("  %-38s %5s %5s %5s %6s  %8s %11s %11s %9s %6s\n", "classifier", "TP", "FP", "FN", "TN", "accuracy",
                "sensitivity", "specificity", "precision", "F1");
    auto row = [](const char* name, const Confusion& c) {
        std::printf("  %-38s %5zu %5zu %5zu %6zu  %8.3f %11.3f %11.3f %9.3f %6.3f\n", name, c.tp, c.fp, c.fn, c.tn,
                    c.accuracy(), c.recall(), c.specificity(), c.precision(), c.f1());
    };
    row("baseline: miss distance < 1 km", baseline);
    row("LSTM, final-risk forecast (all rows)", lstm);
    row("conservative threshold (all rows)", conservative);
    char name[64];
    std::snprintf(name, sizeof name, "OCAS hybrid (%s + fallback)", ocas::to_string(clf.primary().id()));
    row(name, hybrid);
    std::printf("\n  sensitivity = recall = TP/(TP+FN): share of real threats caught. specificity = TN/(TN+FP).\n"
                "  Accuracy is dominated by the ~99%% negatives: a model that never acts scores %.3f.\n",
                double(rows.size() - positives) / double(rows.size()));
    std::printf("\n  Notes: the LSTM forecasts each event's FINAL risk, so this per-CDM label is not its training\n"
                "  target; see ml/lstm_cpp/train_lstm for its own held-out evaluation. The conservative rule's Pc\n"
                "  estimate is physics-derived, so agreement with the provider Pc partly reflects shared physics.\n");
    return 0;
}

// ---------------------------------------------------------------------------
// Final report
// ---------------------------------------------------------------------------
void print_latency(const char* name, const ocas::LatencyProfiler& p) {
    std::printf("  %-26s %8llu %9.3f %9.3f %9.3f %9.3f %9.3f\n", name, static_cast<unsigned long long>(p.count()),
                double(p.min_ns()) / 1000.0, p.mean_ns() / 1000.0, double(p.percentile_ns(0.50)) / 1000.0,
                double(p.percentile_ns(0.99)) / 1000.0, double(p.max_ns()) / 1000.0);
}

void print_verbose_report(const ocas::OcasPipeline& p, const ocas::FlightAuditLogger& log, const ocas::Ansi& a,
                  std::uint64_t loop_allocs, std::uint64_t loop_bytes, double wall_ms, std::size_t ingest_overwrites) {
    const ocas::CycleStats& t = p.totals();
    std::printf("\n%s%s================================ MISSION SUMMARY ================================%s\n",
                a.bold, a.cyan, a.reset);
    std::printf("  cycles %llu | CDMs ingested %zu | rejected input %zu | scored %zu\n",
                static_cast<unsigned long long>(t.cycle), t.ingested, t.rejected_input, t.scored);
    std::printf("  risk model: %s %zu | conservative fallback %zu | actionable CDMs %zu\n",
                ocas::to_string(p.classifier().primary().id()), t.model_used,
                t.fallback_used, t.actionable);
    std::printf("  directives %zu: %scommitted %zu%s | %srolled back %zu%s | %srejected dv-cap %zu | rejected budget "
                "%zu%s | duplicate updates suppressed %zu\n",
                t.directives, a.green, t.committed, a.reset, a.yellow, t.rolled_back, a.reset, a.red,
                t.rejected_dv_cap, t.rejected_budget, a.reset, t.suppressed_duplicates);
    std::printf("  ingest ring-buffer overwrites %zu | heap rejections %llu | heap peak %zu/%zu\n", ingest_overwrites,
                static_cast<unsigned long long>(t.heap_rejected_total), p.heap_peak(), ocas::kThreatHeapCapacity);

    std::printf("\n%s  LATENCY [us]               samples       min      mean       p50       p99       max%s\n",
                a.bold, a.reset);
    print_latency("CDM score + heap insert", p.cdm_latency());
    print_latency("advisory formulate+stage", p.advisory_latency());
    print_latency("full cycle", p.cycle_latency());
    std::printf("  wall time for whole run: %.1f ms (includes terminal and log I/O)\n", wall_ms);

    std::printf("\n%s  MEMORY DETERMINISM%s\n", a.bold, a.reset);
    std::printf("  operator new calls inside cyclic loop: %s%llu%s (%llu bytes)\n", loop_allocs == 0 ? a.green : a.red,
                static_cast<unsigned long long>(loop_allocs), a.reset, static_cast<unsigned long long>(loop_bytes));
    std::printf("  pipeline static footprint: %zu bytes (ring buffer, heap, state stack, ledger pool, profilers)\n",
                sizeof(ocas::OcasPipeline));

    const ocas::MissionLedger& L = p.ledger();
    std::printf("\n%s  MISSION LEDGER%s  %zu records (evicted %llu)\n", a.bold, a.reset, L.size(),
                static_cast<unsigned long long>(L.evicted_count()));
    std::printf("  forward traversal, first 5 committed:\n");
    std::size_t shown = 0;
    L.traverse_forward([&](const ocas::LedgerRecord& r) {
        if (shown >= 5 || r.status != ocas::AdvisoryStatus::Committed) return;
        ++shown;
        std::printf("    cycle %4llu  alert %5u  %-11s dv %.4f m/s  %s\n", static_cast<unsigned long long>(r.cycle),
                    r.directive.alert_id, ocas::to_string(r.directive.plane), r.directive.delta_v_mps,
                    ocas::to_string(r.directive.urgency));
    });
    std::size_t rolled = 0;
    L.traverse_backward([&](const ocas::LedgerRecord& r) {
        rolled += r.status == ocas::AdvisoryStatus::RolledBack ? 1 : 0;
    });
    const std::uint64_t window_start = t.cycle > 100 ? t.cycle - 100 : 0;
    std::printf("  backward traversal: committed dv total %.4f m/s | last 100 cycles %.4f m/s | rolled-back records "
                "%zu\n",
                L.committed_dv_total(), L.committed_dv_since_cycle(window_start), rolled);
    std::printf("  spacecraft dv budget remaining %.4f m/s | memento rollbacks %llu\n",
                p.spacecraft().state().dv_budget_mps, static_cast<unsigned long long>(p.caretaker().rollbacks()));
    std::printf("\n  audit log: %s (%llu records appended this run)\n", log.path().c_str(),
                static_cast<unsigned long long>(log.records_written()));
}

void print_summary(const ocas::OcasPipeline& p, const ocas::FlightAuditLogger& log,
                   const ocas::TerminalTelemetry& term, bool quiet, std::size_t overwrites, const ocas::Ansi& a) {
    const ocas::CycleStats& t = p.totals();
    if (!quiet && term.advisories_hidden() > 0) {
        std::printf("  %s... and %llu more (all listed in %s, or run with --verbose)%s\n", a.dim,
                    static_cast<unsigned long long>(term.advisories_hidden()), log.path().c_str(), a.reset);
    }
    const double budget = p.ledger().committed_dv_total() + p.spacecraft().state().dv_budget_mps;
    std::printf("%s%sRESULT%s\n", quiet ? "" : "\n", a.bold, a.reset);
    std::printf("  Threats checked   %s", commas(t.scored));
    if (t.model_used > 0) std::printf("  (LSTM %s, safety rule %s)\n", commas(t.model_used), commas(t.fallback_used));
    else std::printf("  (all by the safety rule)\n");
    if (overwrites > 0) {
        std::printf("  %sInput overflow    %s oldest alerts dropped (buffer holds 64)%s\n", a.yellow, commas(overwrites),
                    a.reset);
    }
    std::printf("  Dangerous         %s\n", commas(t.actionable));
    std::printf("  Manoeuvres        %zu planned: %s%zu sent to OEM%s", t.directives, a.green, t.committed, a.reset);
    if (t.rolled_back) std::printf(", %s%zu aborted%s", a.yellow, t.rolled_back, a.reset);
    if (t.rejected_dv_cap) std::printf(", %s%zu too large%s", a.red, t.rejected_dv_cap, a.reset);
    if (t.rejected_budget) std::printf(", %s%zu no fuel left%s", a.red, t.rejected_budget, a.reset);
    std::printf("\n  Repeat alerts     %s ignored (threat already handled)\n", commas(t.suppressed_duplicates));
    std::printf("  Fuel used         %.3f of %.2f m/s\n", p.ledger().committed_dv_total(), budget);
    std::printf("  Speed             %.2f ms per cycle (99th percentile)\n",
                double(p.cycle_latency().percentile_ns(0.99)) / 1.0e6);
    std::printf("  Log               %s\n", log.path().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    // Block-buffered stdout: telemetry lines never force a write syscall each.
    static char stdout_buffer[1 << 16];
    std::setvbuf(stdout, stdout_buffer, _IOFBF, sizeof stdout_buffer);

    CliOptions opt;
    if (!parse_cli(argc, argv, opt)) {
        usage(argv[0]);
        return 2;
    }
    const ocas::Ansi a = ocas::make_ansi(opt.color && isatty(STDOUT_FILENO));

    std::printf("%s%sOCAS%s Autonomous Orbital Collision Avoidance System\n", a.bold, a.cyan, a.reset);

    // ---- init phase: allocation allowed ---------------------------------
    ocas::PipelineConfig cfg;
    std::string err;
    // The LSTM has no compiled-in weights: an untrained network is not a model.
    if (!ocas::ml::load_lstm(opt.lstm_weights, cfg.risk.lstm, err)) {
        std::fprintf(stderr, "error: LSTM weights did not load: %s (train with ml/lstm_cpp/train_lstm)\n", err.c_str());
        return 1;
    }
    std::vector<ocas::ConjunctionTelemetry> rows;
    ocas::CdmLoadReport rep;
    if (!ocas::load_cdm_csv(opt.input, rows, rep, opt.max_cdms)) {
        std::fprintf(stderr, "error: %s\n", rep.error.c_str());
        return 1;
    }
    if (opt.verbose) {
        std::printf("[INIT] loaded %zu CDMs from %s (malformed %zu, missing covariance %zu)\n", rep.loaded,
                    opt.input.c_str(), rep.malformed, rep.missing_covariance);
        std::printf("[INIT] LSTM loaded from %s | decision threshold %.4f\n", opt.lstm_weights.c_str(),
                    cfg.risk.lstm.threshold);
    } else {
        std::printf("Input %s (%s conjunction alerts)\n", opt.input.c_str(), commas(rep.loaded));
    }

    if (opt.evaluate) {
        ocas::OrbitalRegime r{};
        const bool leo = ocas::RegimeFactory::classify(opt.altitude_km, r) && r == ocas::OrbitalRegime::LEO;
        static const ocas::RiskClassifier clf(cfg.risk, leo);  // ~0.6 MB LSTM state store: static
        const int rc = run_evaluation(rows, clf, opt.verbose, a);
        std::fflush(stdout);
        return rc;
    }

    cfg.altitude_km = opt.altitude_km;
    cfg.dv_budget_mps = opt.dv_budget_mps;
    cfg.max_advisories_per_cycle = opt.max_advisories;
    cfg.abort_every_n = opt.abort_every;
    cfg.input_label = opt.input.c_str();

    static ocas::OcasPipeline pipeline;  // ~0.3 MB of fixed buffers: static, not stack
    ocas::FlightAuditLogger logger(opt.log, opt.log_all_threats);
    if (!logger.is_open()) {
        std::fprintf(stderr, "warning: cannot open audit log %s\n", opt.log.c_str());
    }
    const ocas::Detail detail = opt.verbose ? ocas::Detail::Verbose
                                : opt.quiet ? ocas::Detail::Quiet
                                            : ocas::Detail::Normal;
    ocas::TerminalTelemetry terminal(opt.color && isatty(STDOUT_FILENO), detail, opt.summary_every);
    pipeline.attach(&terminal);
    pipeline.attach(&logger);
    if (!pipeline.initialize(cfg, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    // ---- cyclic real-time loop: no heap allocation permitted ------------
    const std::uint64_t allocs_before = ocas::alloc_tracker::allocation_count();
    const std::uint64_t bytes_before = ocas::alloc_tracker::allocated_bytes();
    const auto wall_start = ocas::SteadyClock::now();
    std::size_t overwrites = 0;
    for (std::size_t i = 0; i < rows.size();) {
        const std::size_t n = std::min(opt.batch, rows.size() - i);
        for (std::size_t k = 0; k < n; ++k) {
            if (pipeline.ingest(rows[i + k]) == ocas::PushResult::OverwroteOldest) ++overwrites;
        }
        i += n;
        pipeline.run_cycle();
    }
    pipeline.finish();
    const double wall_ms = static_cast<double>(ocas::elapsed_ns(wall_start)) / 1.0e6;
    const std::uint64_t loop_allocs = ocas::alloc_tracker::allocation_count() - allocs_before;
    const std::uint64_t loop_bytes = ocas::alloc_tracker::allocated_bytes() - bytes_before;
    // ---- end of cyclic loop ---------------------------------------------

    if (opt.verbose) print_verbose_report(pipeline, logger, a, loop_allocs, loop_bytes, wall_ms, overwrites);
    else print_summary(pipeline, logger, terminal, opt.quiet, overwrites, a);
    std::fflush(stdout);
    return loop_allocs == 0 ? 0 : 3;
}
