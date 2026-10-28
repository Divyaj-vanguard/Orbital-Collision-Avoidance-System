// OCAS flight executive. Default: stream CDMs through the cyclic pipeline.
// --evaluate: score the risk strategies against the provider Pc label offline.
#include "core/alloc_tracker.hpp"
#include "core/cdm_io.hpp"
#include "persistence/flight_audit_logger.hpp"
#include "pipeline/ocas_pipeline.hpp"
#include "telemetry/terminal_telemetry.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
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
    bool quiet = false;
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
        else if (a == "--abort-every" && need(v) && parse_count(v, n)) o.abort_every = static_cast<std::uint32_t>(n);
        else {
            std::fprintf(stderr, "error: bad or incomplete option '%s'\n", a.c_str());
            return false;
        }
    }
    return true;
}

// 24484 -> "24,484". Two rotating buffers, so two calls fit in one printf.
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

struct Confusion {
    std::size_t tp = 0, fp = 0, tn = 0, fn = 0;
    void add(bool predicted, bool actual) {
        if (predicted && actual) ++tp;
        else if (predicted) ++fp;
        else if (actual) ++fn;
        else ++tn;
    }
    static double ratio(std::size_t a, std::size_t b) { return b == 0 ? 0.0 : double(a) / double(b); }
};

void evaluate(const std::vector<ocas::ConjunctionTelemetry>& rows, const ocas::RiskClassifier& clf,
              const ocas::Ansi& a) {
    constexpr double kLabelPc = 1.0e-4;
    Confusion lstm, conservative, hybrid, baseline;
    std::size_t positives = 0;
    for (const auto& t : rows) {
        if (!(t.time_to_tca_sec > 0.0) || !(t.miss_distance_m >= 0.0)) continue;
        const bool label = t.collision_prob > kLabelPc;
        positives += label ? 1 : 0;
        // Each strategy runs exactly once per row: the LSTM is stateful.
        const bool p_lstm = clf.lstm().assess(t).actionable;
        const bool p_con = clf.conservative().assess(t).actionable;
        lstm.add(p_lstm, label);
        conservative.add(p_con, label);
        hybrid.add(clf.select(t) == ocas::ModelSelection::InDomain ? p_lstm : p_con, label);
        baseline.add(t.miss_distance_m < 1000.0, label);
    }
    std::printf("%sMODEL CHECK%s  %s real ESA alerts, %s truly dangerous (provider Pc > 1e-4)\n\n", a.bold,
                a.reset, commas(rows.size()), commas(positives));
    std::printf("  %-28s %9s %12s %12s %10s %7s\n", "", "accuracy", "sensitivity", "specificity", "precision", "F1");
    auto line = [](const char* name, const Confusion& c) {
        const double p = Confusion::ratio(c.tp, c.tp + c.fp), r = Confusion::ratio(c.tp, c.tp + c.fn);
        std::printf("  %-28s %9.3f %12.3f %12.3f %10.3f %7.3f\n", name, Confusion::ratio(c.tp + c.tn, c.tp + c.fp + c.tn + c.fn),
                    r, Confusion::ratio(c.tn, c.tn + c.fp), p, p + r == 0.0 ? 0.0 : 2.0 * p * r / (p + r));
    };
    line("Simple rule: miss < 1 km", baseline);
    line("LSTM", lstm);
    line("Safety rule", conservative);
    line("OCAS (LSTM + safety rule)", hybrid);
    std::printf("\n  sensitivity = dangerous alerts caught | specificity = safe alerts correctly left alone\n"
                "  The LSTM forecasts each event's FINAL risk, so this per-CDM label is not its training target.\n");
}

void print_summary(const ocas::OcasPipeline& p, const ocas::FlightAuditLogger& log,
                   const ocas::TerminalTelemetry& term, const CliOptions& opt, std::size_t overwrites,
                   std::uint64_t loop_allocs, const ocas::Ansi& a) {
    const ocas::CycleStats& t = p.totals();
    if (!opt.quiet && term.advisories_hidden() > 0) {
        std::printf("  %s... and %llu more (all listed in %s)%s\n", a.dim,
                    static_cast<unsigned long long>(term.advisories_hidden()), log.path().c_str(), a.reset);
    }
    std::printf("%s%sRESULT%s\n", opt.quiet ? "" : "\n", a.bold, a.reset);
    std::printf("  Threats checked   %s", commas(t.scored));
    if (t.model_used > 0) std::printf("  (LSTM %s, safety rule %s)\n", commas(t.model_used), commas(t.fallback_used));
    else std::printf("  (all by the safety rule)\n");
    if (overwrites > 0) {
        std::printf("  %sInput overflow    %s oldest alerts dropped (buffer holds %zu)%s\n", a.yellow,
                    commas(overwrites), ocas::OcasPipeline::kIngestCapacity, a.reset);
    }
    std::printf("  Dangerous         %s\n", commas(t.actionable));
    std::printf("  Manoeuvres        %zu planned: %s%zu sent to OEM%s", t.directives, a.green, t.committed, a.reset);
    if (t.rolled_back) std::printf(", %s%zu aborted%s", a.yellow, t.rolled_back, a.reset);
    if (t.rejected_dv_cap) std::printf(", %s%zu too large%s", a.red, t.rejected_dv_cap, a.reset);
    if (t.rejected_budget) std::printf(", %s%zu no fuel left%s", a.red, t.rejected_budget, a.reset);
    std::printf("\n  Repeat alerts     %s ignored (threat already handled)\n", commas(t.suppressed_duplicates));
    // From the spacecraft state, not the ledger: the ledger drops records beyond its capacity.
    std::printf("  Fuel used         %.3f of %.2f m/s\n", opt.dv_budget_mps - p.spacecraft().state().dv_budget_mps,
                opt.dv_budget_mps);
    std::printf("  Speed             score+rank %.2f us mean | cycle %.3f ms p50, %.3f ms p99\n",
                p.cdm_latency().mean_ns() / 1.0e3, double(p.cycle_latency().percentile_ns(0.50)) / 1.0e6,
                double(p.cycle_latency().percentile_ns(0.99)) / 1.0e6);
    std::printf("  Heap allocations  %s%llu inside the cyclic loop%s\n", loop_allocs == 0 ? a.green : a.red,
                static_cast<unsigned long long>(loop_allocs), a.reset);
    std::printf("  Log               %s\n", log.path().c_str());
}

}  // namespace

int main(int argc, char** argv) {
    static char stdout_buffer[1 << 16];  // block-buffered: no write syscall per telemetry line
    std::setvbuf(stdout, stdout_buffer, _IOFBF, sizeof stdout_buffer);

    CliOptions opt;
    if (!parse_cli(argc, argv, opt)) {
        usage(argv[0]);
        return 2;
    }
    const bool color = opt.color && isatty(STDOUT_FILENO);
    const ocas::Ansi a = ocas::make_ansi(color);
    std::printf("%s%sOCAS%s Autonomous Orbital Collision Avoidance System\n", a.bold, a.cyan, a.reset);

    ocas::PipelineConfig cfg;
    std::string err;
    if (!ocas::ml::load_lstm(opt.lstm_weights, cfg.lstm, err)) {
        std::fprintf(stderr, "error: LSTM weights did not load: %s (train with ml/lstm_cpp/train_lstm)\n", err.c_str());
        return 1;
    }
    std::vector<ocas::ConjunctionTelemetry> rows;
    ocas::CdmLoadReport rep;
    if (!ocas::load_cdm_csv(opt.input, rows, rep, opt.max_cdms)) {
        std::fprintf(stderr, "error: %s\n", rep.error.c_str());
        return 1;
    }
    std::printf("Input %s (%s conjunction alerts", opt.input.c_str(), commas(rep.loaded));
    if (rep.malformed > 0) std::printf(", %s malformed rows skipped", commas(rep.malformed));
    std::printf(")\n");

    if (opt.evaluate) {
        ocas::OrbitalRegime r{};
        const bool leo = ocas::RegimeFactory::classify(opt.altitude_km, r) && r == ocas::OrbitalRegime::LEO;
        static const ocas::RiskClassifier clf(cfg.lstm, leo);  // ~0.6 MB state store: static, not stack
        evaluate(rows, clf, a);
        std::fflush(stdout);
        return 0;
    }

    cfg.altitude_km = opt.altitude_km;
    cfg.dv_budget_mps = opt.dv_budget_mps;
    cfg.max_advisories_per_cycle = opt.max_advisories;
    cfg.abort_every_n = opt.abort_every;
    cfg.input_label = opt.input.c_str();

    static ocas::OcasPipeline pipeline;  // ~0.9 MB of fixed buffers: static, not stack
    ocas::FlightAuditLogger logger(opt.log, opt.log_all_threats);
    if (!logger.is_open()) std::fprintf(stderr, "warning: cannot open audit log %s\n", opt.log.c_str());
    ocas::TerminalTelemetry terminal(color, opt.quiet);
    pipeline.attach(&terminal);
    pipeline.attach(&logger);
    if (!pipeline.initialize(cfg, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    // Cyclic loop: no heap allocation permitted. The process exits 3 if one happens.
    const std::uint64_t allocs_before = ocas::alloc_tracker::allocation_count();
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
    const std::uint64_t loop_allocs = ocas::alloc_tracker::allocation_count() - allocs_before;

    print_summary(pipeline, logger, terminal, opt, overwrites, loop_allocs, a);
    std::fflush(stdout);
    return loop_allocs == 0 ? 0 : 3;
}
