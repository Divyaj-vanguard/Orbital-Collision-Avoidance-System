// OCAS flight executive. Default: stream CDMs through the cyclic pipeline.
// --evaluate: score the risk models against the provider Pc label offline.
#include "alloc_tracker.hpp"
#include "cdm_io.hpp"
#include "pipeline.hpp"

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
    std::size_t abort_every = 0;
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
        "  --batch N             CDMs ingested per cycle       [32]  (>64 forces input overwrite)\n"
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
    out = static_cast<std::size_t>(std::strtoull(s, &end, 10));
    return end != s && *end == '\0' && s[0] != '-';
}

bool parse_cli(int argc, char** argv, CliOptions& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const char* v = nullptr;
        auto need = [&] { return i + 1 < argc && (v = argv[++i]); };
        if (a == "--help" || a == "-h") { usage(argv[0]); std::exit(0); }
        else if (a == "--quiet") o.quiet = true;
        else if (a == "--no-color") o.color = false;
        else if (a == "--evaluate") o.evaluate = true;
        else if (a == "--log-all-threats") o.log_all_threats = true;
        else if (a == "--input" && need()) o.input = v;
        else if (a == "--lstm-weights" && need()) o.lstm_weights = v;
        else if (a == "--log" && need()) o.log = v;
        else if (a == "--altitude-km" && need() && parse_number(v, o.altitude_km)) {}
        else if (a == "--dv-budget" && need() && parse_number(v, o.dv_budget_mps)) {}
        else if (a == "--batch" && need() && parse_count(v, o.batch) && o.batch > 0) {}
        else if (a == "--max-cdms" && need() && parse_count(v, o.max_cdms)) {}
        else if (a == "--max-advisories" && need() && parse_count(v, o.max_advisories)) {}
        else if (a == "--abort-every" && need() && parse_count(v, o.abort_every)) {}
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

double ratio(std::size_t a, std::size_t b) { return b == 0 ? 0.0 : double(a) / double(b); }

struct Confusion {
    std::size_t tp = 0, fp = 0, tn = 0, fn = 0;
    void add(bool predicted, bool actual) { ++(predicted ? (actual ? tp : fp) : (actual ? fn : tn)); }
};

void evaluate(const std::vector<ocas::ConjunctionTelemetry>& rows, const ocas::ml::LstmParams& params, bool leo,
              const ocas::Ansi& a) {
    static ocas::LstmModel lstm;  // ~0.6 MB of event state: static, not stack
    lstm.params = params;
    Confusion m_lstm, m_rule, m_hybrid, m_base;
    std::size_t positives = 0;
    for (const auto& t : rows) {
        if (!(t.time_to_tca_sec > 0.0) || !(t.miss_distance_m >= 0.0)) continue;
        const bool label = t.collision_prob > 1.0e-4;
        positives += label;
        // Each model runs exactly once per row: the LSTM is stateful.
        const bool p_lstm = lstm.assess(t).actionable;
        const bool p_rule = ocas::conservative_assess(t).actionable;
        m_lstm.add(p_lstm, label);
        m_rule.add(p_rule, label);
        m_hybrid.add(ocas::select_model(t, leo) == ocas::ModelSelection::InDomain ? p_lstm : p_rule, label);
        m_base.add(t.miss_distance_m < 1000.0, label);
    }
    std::printf("%sMODEL CHECK%s  %s real ESA alerts, %s truly dangerous (provider Pc > 1e-4)\n\n", a.bold,
                a.reset, commas(rows.size()), commas(positives));
    std::printf("  %-28s %9s %12s %12s %10s %7s\n", "", "accuracy", "sensitivity", "specificity", "precision", "F1");
    for (const auto& [name, c] : {std::pair{"Simple rule: miss < 1 km", m_base}, std::pair{"LSTM", m_lstm},
                                  std::pair{"Safety rule", m_rule}, std::pair{"OCAS (LSTM + safety rule)", m_hybrid}}) {
        const double p = ratio(c.tp, c.tp + c.fp), r = ratio(c.tp, c.tp + c.fn);
        std::printf("  %-28s %9.3f %12.3f %12.3f %10.3f %7.3f\n", name, ratio(c.tp + c.tn, c.tp + c.fp + c.tn + c.fn),
                    r, ratio(c.tn, c.tn + c.fp), p, p + r == 0.0 ? 0.0 : 2.0 * p * r / (p + r));
    }
    std::printf("\n  sensitivity = dangerous alerts caught | specificity = safe alerts correctly left alone\n"
                "  The LSTM forecasts each event's FINAL risk, so this per-CDM label is not its training target.\n");
}

}  // namespace

int main(int argc, char** argv) {
    static char stdout_buffer[1 << 16];  // block-buffered: no write syscall per terminal line
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
        evaluate(rows, cfg.lstm, ocas::classify_regime(opt.altitude_km, r) && r == ocas::OrbitalRegime::LEO, a);
        std::fflush(stdout);
        return 0;
    }

    cfg.altitude_km = opt.altitude_km;
    cfg.dv_budget_mps = opt.dv_budget_mps;
    cfg.max_advisories_per_cycle = opt.max_advisories;
    cfg.abort_every_n = static_cast<std::uint32_t>(opt.abort_every);
    cfg.input_label = opt.input.c_str();

    static ocas::Pipeline pipeline;  // ~0.6 MB, mostly LSTM event state: static, not stack
    ocas::Recorder rec(opt.log, opt.log_all_threats, opt.quiet, color);
    if (!rec.is_open()) std::fprintf(stderr, "warning: cannot open audit log %s\n", opt.log.c_str());
    if (!pipeline.initialize(cfg, &rec, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::vector<double> cycle_us;
    cycle_us.reserve((rows.size() + opt.batch - 1) / opt.batch);

    // Cyclic loop: no heap allocation permitted. The process exits 3 if one happens.
    const std::uint64_t allocs_before = ocas::allocation_count();
    std::size_t overwrites = 0;
    for (std::size_t i = 0; i < rows.size(); i += opt.batch) {
        for (std::size_t k = i; k < std::min(i + opt.batch, rows.size()); ++k) overwrites += pipeline.ingest(rows[k]);
        cycle_us.push_back(pipeline.run_cycle().cycle_latency_us);
    }
    pipeline.finish();
    const std::uint64_t loop_allocs = ocas::allocation_count() - allocs_before;

    const ocas::CycleStats& t = pipeline.totals();
    if (!opt.quiet && rec.advisories_hidden() > 0) {
        std::printf("  %s... and %llu more (all listed in %s)%s\n", a.dim,
                    static_cast<unsigned long long>(rec.advisories_hidden()), rec.path().c_str(), a.reset);
    }
    std::sort(cycle_us.begin(), cycle_us.end());
    auto pct_ms = [&](double q) {
        return cycle_us.empty() ? 0.0 : cycle_us[static_cast<std::size_t>(q * double(cycle_us.size() - 1))] / 1e3;
    };
    std::printf("%s%sRESULT%s\n", opt.quiet ? "" : "\n", a.bold, a.reset);
    std::printf("  Threats checked   %s", commas(t.scored));
    if (t.model_used > 0) std::printf("  (LSTM %s, safety rule %s)\n", commas(t.model_used), commas(t.fallback_used));
    else std::printf("  (all by the safety rule)\n");
    if (overwrites > 0) {
        std::printf("  %sInput overflow    %s oldest alerts dropped (buffer holds %zu)%s\n", a.yellow,
                    commas(overwrites), ocas::Pipeline::kInboxCapacity, a.reset);
    }
    std::printf("  Dangerous         %s\n", commas(t.actionable));
    std::printf("  Manoeuvres        %zu planned: %s%zu sent to OEM%s", t.directives, a.green, t.committed, a.reset);
    if (t.rolled_back) std::printf(", %s%zu aborted%s", a.yellow, t.rolled_back, a.reset);
    if (t.rejected_dv_cap) std::printf(", %s%zu too large%s", a.red, t.rejected_dv_cap, a.reset);
    if (t.rejected_budget) std::printf(", %s%zu no fuel left%s", a.red, t.rejected_budget, a.reset);
    std::printf("\n  Repeat alerts     %s ignored (threat already handled)\n", commas(t.suppressed_duplicates));
    std::printf("  Fuel used         %.3f of %.2f m/s\n", opt.dv_budget_mps - pipeline.dv_budget_mps(), opt.dv_budget_mps);
    std::printf("  Speed             score+rank %.2f us mean | cycle %.3f ms p50, %.3f ms p99\n",
                pipeline.mean_score_ns() / 1e3, pct_ms(0.50), pct_ms(0.99));
    std::printf("  Heap allocations  %s%llu inside the cyclic loop%s\n", loop_allocs == 0 ? a.green : a.red,
                static_cast<unsigned long long>(loop_allocs), a.reset);
    std::printf("  Log               %s\n", rec.path().c_str());
    std::fflush(stdout);
    return loop_allocs == 0 ? 0 : 3;
}
