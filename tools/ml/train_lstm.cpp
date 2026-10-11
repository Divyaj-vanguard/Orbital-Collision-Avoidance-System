// ============================================================================
// OCAS ground tool: train the LSTM final-risk forecaster (4 -> LSTM(16) -> 1).
// ============================================================================
// Task   : the ESA Kelvins challenge framing (Uriot et al., 2022). Given an
//          event's CDM history up to 2 days before TCA, forecast whether its
//          FINAL CDM (closest to TCA) is high risk: log10 Pc >= -6, the
//          challenge's own definition. The exact risk is read from the raw ESA
//          file (train_data.csv); train_clean.csv rounds Pc to 6 decimals.
//          (Pc > 1e-4 leaves only 13 positive events of 13,154: not learnable.)
// Inputs : per CDM, the 4 standardised features of include/ml/neural.hpp (no Pc input).
//          Only CDMs with t_TCA >= 2 days and inside the model domain.
// Split  : by EVENT 70 / 15 / 15, seed 2026.
// Train  : loss on every prefix (online use predicts after each CDM), weighted
//          BCE, full BPTT, Adam, gradient clip 5, early stopping on validation AP
//          of the prediction at the last input CDM.
// Report : held-out test events vs. baselines at the last input CDM:
//          miss distance < 1 km and "latest log10 Pc >= -6"
//          (a reference that uses the provider Pc, which OCAS models do not).
// The official ESA test file cannot score this task: its final CDMs are withheld.
// Usage  : train_lstm [repo_root=.] [--epochs N] [--seed N] [--gradcheck]
// ============================================================================
#include "ml_common.hpp"
#include "risk/risk_strategy.hpp"

#include <cstring>
#include <map>

using namespace ocas;
using namespace ocas::mltools;

namespace {

constexpr std::size_t F = ml::kFeatures, H = ml::kLstmH, G = 4 * H;
constexpr std::size_t oW = 0, oU = oW + G * F, oB = oU + G * H, oWo = oB + G, oBo = oWo + H, kParams = oBo + 1;
constexpr double kHorizon = 2.0 * 86400.0;
constexpr std::size_t kMaxSteps = 64;
constexpr double kHighRiskLog10 = -6.0;

/// event -> (time_to_tca [s], log10 Pc) for every CDM in the raw ESA file.
bool load_raw_risk(const std::string& path, std::map<std::uint32_t, std::vector<std::pair<double, double>>>& out) {
    std::ifstream in(path);
    std::string line;
    if (!in || !std::getline(in, line)) return false;
    if (line.rfind("event_id,time_to_tca,mission_id,risk", 0) != 0) return false;
    while (std::getline(in, line)) {
        const char* p = line.c_str();
        char* e = nullptr;
        const double ev = std::strtod(p, &e);
        if (*e != ',') continue;
        const double t_days = std::strtod(e + 1, &e);
        if (*e != ',') continue;
        std::strtod(e + 1, &e);  // mission_id
        if (*e != ',') continue;
        const double risk = std::strtod(e + 1, &e);
        out[static_cast<std::uint32_t>(ev)].push_back({t_days * 86400.0, risk});
    }
    return true;
}

/// log10 Pc of the raw CDM of `event` closest in time to t (CDMs are matched by time).
double risk_at(const std::vector<std::pair<double, double>>& cdms, double t) {
    double best = 1e300, r = -30.0;
    for (const auto& [tt, rr] : cdms)
        if (std::fabs(tt - t) < best) { best = std::fabs(tt - t); r = rr; }
    return r;
}

struct Sequence {
    std::uint32_t event;
    std::vector<ml::Features> x;  ///< standardised
    std::vector<Cdm> raw;
    bool label;                   ///< final CDM high risk
    bool latest_label;            ///< last input CDM high risk (reference baseline)
};

ml::LstmParams to_params(const std::vector<double>& w, const ml::FeatureScaler& s, double thr) {
    ml::LstmParams p{};
    p.scaler = s;
    p.threshold = thr;
    std::copy(w.begin() + oW, w.begin() + oU, p.W.begin());
    std::copy(w.begin() + oU, w.begin() + oB, p.U.begin());
    std::copy(w.begin() + oB, w.begin() + oWo, p.b.begin());
    std::copy(w.begin() + oWo, w.begin() + oBo, p.w_out.begin());
    p.b_out = w[oBo];
    return p;
}

double sig(double z) { return ml::sigmoid(z); }

/// BPTT over one sequence; accumulates gradient, returns mean loss over steps.
double bptt(const std::vector<double>& w, const std::vector<ml::Features>& xs, bool y, double pos_w,
            std::vector<double>& g) {
    const std::size_t T = xs.size();
    std::vector<std::array<double, G>> gate(T);
    std::vector<std::array<double, H>> hs(T + 1), cs(T + 1);  // index 0 = initial zero state
    hs[0].fill(0.0);
    cs[0].fill(0.0);
    std::vector<double> p(T);
    const double wt = y ? pos_w : 1.0, yt = y ? 1.0 : 0.0;
    double loss = 0.0;
    for (std::size_t t = 0; t < T; ++t) {
        for (std::size_t r = 0; r < G; ++r) {
            double a = w[oB + r];
            for (std::size_t i = 0; i < F; ++i) a += w[oW + r * F + i] * xs[t][i];
            for (std::size_t j = 0; j < H; ++j) a += w[oU + r * H + j] * hs[t][j];
            gate[t][r] = a;
        }
        double z = w[oBo];
        for (std::size_t j = 0; j < H; ++j) {
            const double i_ = sig(gate[t][j]), f_ = sig(gate[t][H + j]), g_ = std::tanh(gate[t][2 * H + j]),
                         o_ = sig(gate[t][3 * H + j]);
            gate[t][j] = i_; gate[t][H + j] = f_; gate[t][2 * H + j] = g_; gate[t][3 * H + j] = o_;  // activations
            cs[t + 1][j] = f_ * cs[t][j] + i_ * g_;
            hs[t + 1][j] = o_ * std::tanh(cs[t + 1][j]);
            z += w[oWo + j] * hs[t + 1][j];
        }
        p[t] = sig(z);
        loss += -wt * (y ? std::log(std::max(p[t], 1e-12)) : std::log(std::max(1 - p[t], 1e-12)));
    }
    std::array<double, H> dh_next{}, dc_next{};
    const double invT = 1.0 / double(T);
    for (std::size_t t = T; t-- > 0;) {
        const double dz = wt * (p[t] - yt) * invT;
        g[oBo] += dz;
        std::array<double, H> dh{};
        for (std::size_t j = 0; j < H; ++j) {
            g[oWo + j] += dz * hs[t + 1][j];
            dh[j] = dz * w[oWo + j] + dh_next[j];
        }
        std::array<double, G> da{};
        for (std::size_t j = 0; j < H; ++j) {
            const double i_ = gate[t][j], f_ = gate[t][H + j], g_ = gate[t][2 * H + j], o_ = gate[t][3 * H + j];
            const double tc = std::tanh(cs[t + 1][j]);
            const double dc = dh[j] * o_ * (1 - tc * tc) + dc_next[j];
            da[j] = dc * g_ * i_ * (1 - i_);
            da[H + j] = dc * cs[t][j] * f_ * (1 - f_);
            da[2 * H + j] = dc * i_ * (1 - g_ * g_);
            da[3 * H + j] = dh[j] * tc * o_ * (1 - o_);
            dc_next[j] = dc * f_;
        }
        dh_next.fill(0.0);
        for (std::size_t r = 0; r < G; ++r) {
            g[oB + r] += da[r];
            for (std::size_t i = 0; i < F; ++i) g[oW + r * F + i] += da[r] * xs[t][i];
            for (std::size_t j = 0; j < H; ++j) {
                g[oU + r * H + j] += da[r] * hs[t][j];
                dh_next[j] += da[r] * w[oU + r * H + j];
            }
        }
    }
    return loss * invT;
}

bool gradient_check(SplitMix64& rng) {
    std::vector<double> w(kParams), g(kParams, 0.0), dummy(kParams);
    for (auto& x : w) x = rng.uniform(-0.5, 0.5);
    double worst = 0.0;
    for (int s = 0; s < 4; ++s) {
        std::vector<ml::Features> xs(3 + s);
        for (auto& x : xs) x = {rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-2, 2)};
        const bool y = s % 2;
        std::fill(g.begin(), g.end(), 0.0);
        bptt(w, xs, y, 3.0, g);
        for (std::size_t i = 0; i < kParams; ++i) {
            const double h = 1e-6, keep = w[i];
            w[i] = keep + h;
            const double lp = bptt(w, xs, y, 3.0, dummy);
            w[i] = keep - h;
            const double lm = bptt(w, xs, y, 3.0, dummy);
            w[i] = keep;
            const double num = (lp - lm) / (2 * h);
            worst = std::max(worst, std::fabs(num - g[i]) / std::max(1.0, std::fabs(num) + std::fabs(g[i])));
        }
    }
    std::printf("gradient check: worst relative error %.2e -> %s\n", worst, worst < 1e-5 ? "PASS" : "FAIL");
    return worst < 1e-5;
}

/// Score each sequence at its last input CDM using the FLIGHT strategy class.
std::vector<Scored> score_lstm(const ml::LstmParams& p, const std::vector<Sequence>& seqs) {
    static LstmStrategy s(p);  // ~0.6 MB state store
    s = LstmStrategy(p);
    std::vector<Scored> out;
    for (const auto& q : seqs) {
        double score = 0.0;
        for (const auto& c : q.raw) score = s.assess(to_telemetry(c)).score;
        out.push_back({score, q.label});
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = ".";
    int epochs = 40;
    bool gradcheck = false;
    std::uint64_t seed = 2026;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) { seed = std::strtoull(argv[++i], nullptr, 10); continue; }
        if (!std::strcmp(argv[i], "--epochs") && i + 1 < argc) epochs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gradcheck")) gradcheck = true;
        else root = argv[i];
    }
    SplitMix64 rng{seed};
    if (gradcheck) return gradient_check(rng) ? 0 : 1;

    std::vector<Cdm> all;
    if (!load_esa_clean(root + "/data/train_clean.csv", all)) return 1;
    std::map<std::uint32_t, std::vector<std::pair<double, double>>> raw_risk;
    if (!load_raw_risk(root + "/train_data.csv", raw_risk)) {
        std::cerr << "cannot read " << root << "/train_data.csv (raw ESA Kelvins training file; unzip train_data.zip)\n";
        return 1;
    }
    std::map<std::uint32_t, std::vector<Cdm>> events;
    for (const auto& c : all) events[c.event].push_back(c);

    std::vector<Sequence> split[3];
    std::size_t no_history = 0;
    for (auto& [id, cdms] : events) {
        std::sort(cdms.begin(), cdms.end(), [](const Cdm& a, const Cdm& b) { return a.t > b.t; });
        const auto rr = raw_risk.find(id);
        if (rr == raw_risk.end()) continue;
        double final_t = 1e300, final_r = -30.0;
        for (const auto& [tt, r] : rr->second)
            if (tt < final_t) { final_t = tt; final_r = r; }
        Sequence s{id, {}, {}, final_r >= kHighRiskLog10, false};
        for (const auto& c : cdms) {
            if (c.t >= kHorizon && in_model_domain(c)) s.raw.push_back(c);
        }
        if (s.raw.empty()) { ++no_history; continue; }
        if (s.raw.size() > kMaxSteps) s.raw.erase(s.raw.begin(), s.raw.end() - kMaxSteps);
        s.latest_label = risk_at(rr->second, s.raw.back().t) >= kHighRiskLog10;
        split[static_cast<int>(split_of(id))].push_back(std::move(s));
    }
    auto& train = split[0];
    auto& val = split[1];
    auto& test = split[2];
    std::size_t pos = 0;
    for (const auto& s : train) pos += s.label;
    std::printf("events %zu (%zu without in-domain CDMs >= 2 days) | train %zu (%zu final-high-risk) | val %zu | "
                "test %zu\n",
                events.size(), no_history, train.size(), pos, val.size(), test.size());

    std::vector<ml::Features> raw;
    for (const auto& s : train)
        for (const auto& c : s.raw) raw.push_back(ml::raw_features(to_telemetry(c)));
    const ml::FeatureScaler scaler = fit_scaler(raw);
    for (auto* part : {&train, &val, &test})
        for (auto& s : *part)
            for (const auto& c : s.raw) s.x.push_back(ml::standardize(ml::raw_features(to_telemetry(c)), scaler));

    const double pos_w = std::sqrt(double(train.size() - pos) / double(std::max<std::size_t>(pos, 1)));
    std::vector<double> w(kParams, 0.0), g(kParams, 0.0);
    const double a = 1.0 / std::sqrt(double(H));
    for (std::size_t i = oW; i < oB; ++i) w[i] = rng.uniform(-a, a);
    for (std::size_t j = 0; j < H; ++j) w[oB + H + j] = 1.0;  // forget-gate bias 1
    for (std::size_t j = 0; j < H; ++j) w[oWo + j] = rng.uniform(-a, a);

    Adam opt;
    opt.lr = 2e-3;
    std::vector<std::size_t> order(train.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    double best_ap = -1.0;
    std::vector<double> best = w;
    int best_epoch = 0;
    constexpr std::size_t kBatch = 32;
    std::printf("training: pos_weight %.2f, %d epochs max, batch %zu sequences, Adam lr %.0e\n", pos_w, epochs, kBatch,
                opt.lr);
    for (int e = 1; e <= epochs; ++e) {
        rng.shuffle(order);
        double loss = 0.0;
        for (std::size_t b = 0; b < order.size(); b += kBatch) {
            const std::size_t end = std::min(order.size(), b + kBatch);
            for (std::size_t k = b; k < end; ++k) loss += bptt(w, train[order[k]].x, train[order[k]].label, pos_w, g);
            for (auto& x : g) x /= double(end - b);
            opt.step(w, g, 5.0);
        }
        const double ap = average_precision(score_lstm(to_params(w, scaler, 0.5), val));
        if (ap > best_ap) { best_ap = ap; best = w; best_epoch = e; }
        if (e % 5 == 0 || e == 1) std::printf("  epoch %3d  loss %.4f  val AP %.4f\n", e, loss / double(train.size()), ap);
        if (e - best_epoch >= 10) { std::printf("  early stop at epoch %d\n", e); break; }
    }
    const double thr = best_f1_threshold(score_lstm(to_params(best, scaler, 0.5), val));
    const ml::LstmParams final_p = to_params(best, scaler, thr);
    std::printf("best epoch %d (val AP %.4f), validation F1-optimal threshold %.6f\n", best_epoch, best_ap, thr);

    // ---- Held-out evaluation at the last input CDM ------------------------------
    std::string err;
    std::vector<Scored> base, latest, lstm = score_lstm(final_p, test);
    std::size_t npos = 0;
    for (const auto& s : test) {
        const Cdm& last = s.raw.back();
        npos += s.label;
        base.push_back({-last.d, s.label});
        latest.push_back({s.latest_label ? 1.0 : 0.0, s.label});
    }
    print_header("HELD-OUT TEST EVENTS: forecast final-CDM risk from history >= 2 days before TCA", test.size(), npos);
    print_row("baseline: last miss distance < 1 km", base, -1000.0);
    print_row("LSTM over CDM history (validation threshold)", lstm, thr);
    print_row("reference: latest log10 Pc >= -6 (uses Pc)", latest, 0.5);
    std::printf("  note: the latest-Pc reference uses the provider Pc as input, which OCAS models deliberately do not.\n");

    std::ostringstream comment;
    comment << "OCAS LSTM(16) final-risk forecaster, trained by tools/ml/train_lstm\n"
            << "Data: ESA Kelvins train file, event-level split 70/15/15 (fixed), init/shuffle seed " << seed << ", inputs t_TCA >= 2 days\n"
            << "Label: final CDM log10 Pc >= -6 (ESA challenge definition) | pos_weight " << pos_w << " | best epoch " << best_epoch << "\n"
            << "Threshold: F1-optimal on validation events | validation AP " << best_ap;
    const std::string out = root + "/data/models/lstm_weights.txt";
    if (!ml::save_lstm(out, final_p, comment.str())) { std::cerr << "cannot write " << out << "\n"; return 1; }
    ml::LstmParams check{};
    if (!ml::load_lstm(out, check, err)) { std::cerr << "reload failed: " << err << "\n"; return 1; }
    std::printf("\nsaved %s (reload verified)\n", out.c_str());
    return 0;
}
