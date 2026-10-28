// Offline trainer for the flight LSTM (4 inputs -> 16 hidden -> 1 output). Writes models/lstm_weights.txt.
//
// Task:  the ESA Kelvins challenge framing. From an event's CDMs up to 2 days before closest approach,
//        forecast whether its FINAL CDM is high risk (log10 Pc >= -6). Pc itself is never an input.
// Data:  data/esa/train_data.csv (raw ESA file), split by EVENT 70/15/15 so no event leaks across sets.
// Train: weighted cross-entropy on every prefix, backpropagation through time, Adam, gradient clip 5,
//        early stopping on validation average precision. Threshold = best F1 on validation.
// Usage: train_lstm [repo_root] [--epochs N] [--seed N] [--gradcheck]
#include "neural.hpp"
#include "risk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ocas;

namespace {

constexpr std::size_t F = ml::kFeatures, H = ml::kLstmH, G = 4 * H;
// All weights live in one flat vector: [W | U | b | w_out | b_out].
constexpr std::size_t oW = 0, oU = oW + G * F, oB = oU + G * H, oWo = oB + G, oBo = oWo + H, kParams = oBo + 1;
constexpr double kHorizon = 2.0 * 86400.0;  // only CDMs at least 2 days before TCA are inputs
constexpr std::size_t kMaxSteps = 64;
constexpr double kHighRiskLog10 = -6.0;

struct Rng {  // SplitMix64: same numbers on every platform
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) { return lo + (hi - lo) * (static_cast<double>(next() >> 11) * 0x1.0p-53); }
};

// 0 = train, 1 = validation, 2 = test. Fixed per event, independent of the seed.
int split_of(std::uint32_t event) {
    const auto bucket = Rng{0xC0FFEEull ^ event}.next() % 100;
    return bucket < 70 ? 0 : bucket < 85 ? 1 : 2;
}

// The shipped weights were trained on values rounded to 6 decimals; keep that so retraining reproduces them.
double round6(double x) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", x);
    return std::strtod(buf, nullptr);
}

struct Raw {
    std::uint32_t event;
    double t;     // time to TCA [s]
    double risk;  // log10 Pc
    ConjunctionTelemetry cdm;
    bool usable;  // has non-zero geometry
};

bool load_esa(const std::string& path, std::vector<Raw>& out) {
    std::ifstream in(path);
    std::string line, cell;
    if (!std::getline(in, line)) return false;
    std::vector<std::string> header;
    for (std::istringstream ss(line); std::getline(ss, cell, ',');) header.push_back(cell);
    auto col = [&](const char* name) { return std::find(header.begin(), header.end(), name) - header.begin(); };
    const long id = col("event_id"), tca = col("time_to_tca"), risk = col("risk"), miss = col("miss_distance"),
               speed = col("relative_speed");
    const long sigma[6] = {col("t_sigma_r"), col("t_sigma_t"), col("t_sigma_n"),
                           col("c_sigma_r"), col("c_sigma_t"), col("c_sigma_n")};
    std::vector<std::string> f;
    while (std::getline(in, line)) {
        f.clear();
        for (std::istringstream ss(line); std::getline(ss, cell, ',');) f.push_back(cell);
        f.resize(header.size());
        auto num = [&](long c) {
            const double v = std::strtod(f[c].c_str(), nullptr);
            return std::isnan(v) ? 0.0 : v;  // missing -> 0
        };
        Raw r{};
        r.event = static_cast<std::uint32_t>(num(id));
        r.t = num(tca) * 86400.0;
        r.risk = num(risk);
        r.usable = !(num(miss) == 0.0 && num(speed) == 0.0);
        r.cdm.alert_id = r.event;
        r.cdm.time_to_tca_sec = round6(r.t);
        r.cdm.miss_distance_m = round6(num(miss));
        r.cdm.relative_speed_km_s = round6(num(speed)) / 1000.0;
        for (long c : sigma) r.cdm.cov_trace_m2 += round6(num(c)) * round6(num(c));
        out.push_back(r);
    }
    return true;
}

struct Sequence {
    std::vector<ConjunctionTelemetry> cdms;  // inputs, oldest first
    std::vector<ml::Features> x;             // the same, standardised
    bool label;                              // final CDM is high risk
    bool latest_label;                       // last input CDM is high risk (reference that uses Pc)
};

ml::LstmParams to_params(const std::vector<double>& w, const ml::FeatureScaler& s, double threshold) {
    ml::LstmParams p{};
    p.scaler = s;
    p.threshold = threshold;
    std::copy(w.begin() + oW, w.begin() + oU, p.W.begin());
    std::copy(w.begin() + oU, w.begin() + oB, p.U.begin());
    std::copy(w.begin() + oB, w.begin() + oWo, p.b.begin());
    std::copy(w.begin() + oWo, w.begin() + oBo, p.w_out.begin());
    p.b_out = w[oBo];
    return p;
}

// Forward and backward pass over one sequence. Adds the gradient to g and returns the mean loss.
// Forward is the same maths as ml::lstm_step; the loss is applied after every step.
double bptt(const std::vector<double>& w, const std::vector<ml::Features>& xs, bool y, double pos_weight,
            std::vector<double>& g) {
    const std::size_t T = xs.size();
    std::vector<std::array<double, G>> gate(T);                // gate activations i, f, g, o per step
    std::vector<std::array<double, H>> hs(T + 1), cs(T + 1);  // index 0 = zero start state
    std::vector<double> p(T);
    const double wt = y ? pos_weight : 1.0;
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
            const double i_ = ml::sigmoid(gate[t][j]), f_ = ml::sigmoid(gate[t][H + j]);
            const double g_ = std::tanh(gate[t][2 * H + j]), o_ = ml::sigmoid(gate[t][3 * H + j]);
            gate[t][j] = i_, gate[t][H + j] = f_, gate[t][2 * H + j] = g_, gate[t][3 * H + j] = o_;
            cs[t + 1][j] = f_ * cs[t][j] + i_ * g_;
            hs[t + 1][j] = o_ * std::tanh(cs[t + 1][j]);
            z += w[oWo + j] * hs[t + 1][j];
        }
        p[t] = ml::sigmoid(z);
        loss += -wt * std::log(std::max(y ? p[t] : 1 - p[t], 1e-12));
    }
    std::array<double, H> dh_next{}, dc_next{};
    const double invT = 1.0 / double(T);
    for (std::size_t t = T; t-- > 0;) {
        const double dz = wt * (p[t] - (y ? 1.0 : 0.0)) * invT;
        g[oBo] += dz;
        std::array<double, H> dh{};
        for (std::size_t j = 0; j < H; ++j) {
            g[oWo + j] += dz * hs[t + 1][j];
            dh[j] = dz * w[oWo + j] + dh_next[j];
        }
        std::array<double, G> da{};  // gradient at each gate's pre-activation
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

// Compares bptt's gradient with a numerical one (finite differences) on random data.
bool gradient_check(Rng& rng) {
    std::vector<double> w(kParams), g(kParams), unused(kParams);
    for (auto& x : w) x = rng.uniform(-0.5, 0.5);
    double worst = 0.0;
    for (int s = 0; s < 4; ++s) {
        std::vector<ml::Features> xs(3 + s);
        for (auto& x : xs) x = {rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-2, 2), rng.uniform(-2, 2)};
        std::fill(g.begin(), g.end(), 0.0);
        bptt(w, xs, s % 2, 3.0, g);
        for (std::size_t i = 0; i < kParams; ++i) {
            const double h = 1e-6, keep = w[i];
            w[i] = keep + h;
            const double up = bptt(w, xs, s % 2, 3.0, unused);
            w[i] = keep - h;
            const double down = bptt(w, xs, s % 2, 3.0, unused);
            w[i] = keep;
            const double numeric = (up - down) / (2 * h);
            worst = std::max(worst, std::fabs(numeric - g[i]) / std::max(1.0, std::fabs(numeric) + std::fabs(g[i])));
        }
    }
    std::printf("gradient check: worst relative error %.2e -> %s\n", worst, worst < 1e-5 ? "PASS" : "FAIL");
    return worst < 1e-5;
}

struct Scored {
    double score;
    bool label;
};

// Score each sequence after its last input CDM, using the same LstmModel class the flight code uses.
std::vector<Scored> score(const ml::LstmParams& p, const std::vector<Sequence>& seqs) {
    static LstmModel model;  // ~0.6 MB: static, not stack
    model = LstmModel(p);
    std::vector<Scored> out;
    for (const auto& s : seqs) {
        double last = 0.0;
        for (const auto& c : s.cdms) last = model.assess(c).score;
        out.push_back({last, s.label});
    }
    return out;
}

// Area under the precision-recall curve. Suits rare positives better than accuracy.
double average_precision(std::vector<Scored> s) {
    std::sort(s.begin(), s.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    double ap = 0.0;
    std::size_t tp = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i].label) ap += double(++tp) / double(i + 1);
    }
    return tp == 0 ? 0.0 : ap / double(tp);
}

// The threshold (one of the scores) with the best F1.
double best_f1_threshold(std::vector<Scored> s) {
    std::sort(s.begin(), s.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    std::size_t pos = 0, tp = 0, fp = 0;
    for (const auto& x : s) pos += x.label;
    double best_f1 = -1.0, best = 1.0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i].label ? ++tp : ++fp;
        if (i + 1 < s.size() && s[i + 1].score == s[i].score) continue;  // ties move together
        const double f1 = 2.0 * double(tp) / double(2 * tp + fp + (pos - tp));
        if (f1 > best_f1) best_f1 = f1, best = s[i].score;
    }
    return best;
}

void print_row(const char* name, const std::vector<Scored>& s, double threshold) {
    std::size_t tp = 0, fp = 0, fn = 0;
    for (const auto& x : s) {
        const bool predicted = x.score >= threshold;
        tp += predicted && x.label, fp += predicted && !x.label, fn += !predicted && x.label;
    }
    const double precision = tp + fp ? double(tp) / double(tp + fp) : 0.0;
    const double recall = tp + fn ? double(tp) / double(tp + fn) : 0.0;
    const double f1 = precision + recall > 0 ? 2 * precision * recall / (precision + recall) : 0.0;
    std::printf("  %-44s %6.3f %5zu %5zu %5zu %6.3f %6.3f %6.3f\n", name, average_precision(s), tp, fp, fn, precision,
                recall, f1);
}

}  // namespace

int main(int argc, char** argv) {
    std::string root = ".";
    int epochs = 40;
    bool gradcheck = false;
    std::uint64_t seed = 2026;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--seed") && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--epochs") && i + 1 < argc) epochs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--gradcheck")) gradcheck = true;
        else root = argv[i];
    }
    Rng rng{seed};
    if (gradcheck) return gradient_check(rng) ? 0 : 1;

    std::vector<Raw> rows;
    if (!load_esa(root + "/data/esa/train_data.csv", rows)) {
        std::fprintf(stderr, "cannot read %s/data/esa/train_data.csv (raw ESA Kelvins training file)\n", root.c_str());
        return 1;
    }
    std::map<std::uint32_t, std::vector<const Raw*>> events;
    for (const Raw& r : rows) events[r.event].push_back(&r);

    // One sequence per event: its in-domain CDMs >= 2 days before TCA, labelled by its final CDM.
    std::vector<Sequence> split[3];
    std::size_t no_history = 0;
    for (auto& [id, cdms] : events) {
        const Raw* final_cdm = *std::min_element(cdms.begin(), cdms.end(), [](auto a, auto b) { return a->t < b->t; });
        std::vector<const Raw*> inputs;
        for (const Raw* r : cdms) {
            if (r->usable) inputs.push_back(r);
        }
        std::sort(inputs.begin(), inputs.end(), [](auto a, auto b) { return a->cdm.time_to_tca_sec > b->cdm.time_to_tca_sec; });
        Sequence s{{}, {}, final_cdm->risk >= kHighRiskLog10, false};
        for (const Raw* r : inputs) {
            if (r->cdm.time_to_tca_sec >= kHorizon && select_model(r->cdm, true) == ModelSelection::InDomain) {
                s.cdms.push_back(r->cdm);
            }
        }
        if (s.cdms.empty()) {
            ++no_history;
            continue;
        }
        if (s.cdms.size() > kMaxSteps) s.cdms.erase(s.cdms.begin(), s.cdms.end() - kMaxSteps);
        // Reference baseline: the provider's Pc of the raw CDM closest in time to the last input.
        const double t_last = s.cdms.back().time_to_tca_sec;
        const Raw* latest = *std::min_element(cdms.begin(), cdms.end(), [&](auto a, auto b) {
            return std::fabs(a->t - t_last) < std::fabs(b->t - t_last);
        });
        s.latest_label = latest->risk >= kHighRiskLog10;
        split[split_of(id)].push_back(std::move(s));
    }
    auto& [train, val, test] = split;
    std::size_t pos = 0;
    for (const auto& s : train) pos += s.label;
    std::printf("events %zu (%zu without in-domain CDMs >= 2 days) | train %zu (%zu final-high-risk) | val %zu | test %zu\n",
                events.size(), no_history, train.size(), pos, val.size(), test.size());

    // Standardise inputs with the training set's mean and standard deviation.
    ml::FeatureScaler scaler;
    std::vector<ml::Features> raw;
    for (const auto& s : train)
        for (const auto& c : s.cdms) raw.push_back(ml::raw_features(c));
    for (std::size_t i = 0; i < F; ++i) {
        double mean = 0.0, var = 0.0;
        for (const auto& r : raw) mean += r[i];
        mean /= double(raw.size());
        for (const auto& r : raw) var += (r[i] - mean) * (r[i] - mean);
        scaler.mean[i] = mean;
        scaler.stddev[i] = std::max(1e-9, std::sqrt(var / double(raw.size())));
    }
    for (auto* part : {&train, &val, &test})
        for (auto& s : *part)
            for (const auto& c : s.cdms) s.x.push_back(ml::standardize(ml::raw_features(c), scaler));

    // Positive events are rare, so their loss is weighted up.
    const double pos_weight = std::sqrt(double(train.size() - pos) / double(std::max<std::size_t>(pos, 1)));
    std::vector<double> w(kParams, 0.0), g(kParams, 0.0);
    const double a = 1.0 / std::sqrt(double(H));
    for (std::size_t i = oW; i < oB; ++i) w[i] = rng.uniform(-a, a);
    for (std::size_t j = 0; j < H; ++j) w[oB + H + j] = 1.0;  // forget-gate bias 1: remember by default
    for (std::size_t j = 0; j < H; ++j) w[oWo + j] = rng.uniform(-a, a);

    // Adam optimiser state.
    const double lr = 2e-3, b1 = 0.9, b2 = 0.999;
    std::vector<double> m(kParams, 0.0), v(kParams, 0.0);
    long step = 0;
    constexpr std::size_t kBatch = 32;

    std::vector<std::size_t> order(train.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    double best_ap = -1.0;
    std::vector<double> best = w;
    int best_epoch = 0;
    std::printf("training: pos_weight %.2f, %d epochs max, batch %zu sequences, Adam lr %.0e\n", pos_weight, epochs,
                kBatch, lr);
    for (int e = 1; e <= epochs; ++e) {
        for (std::size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[rng.next() % i]);  // shuffle
        double loss = 0.0;
        for (std::size_t b = 0; b < order.size(); b += kBatch) {
            const std::size_t end = std::min(order.size(), b + kBatch);
            for (std::size_t k = b; k < end; ++k) loss += bptt(w, train[order[k]].x, train[order[k]].label, pos_weight, g);
            double norm = 0.0;
            for (auto& x : g) x /= double(end - b), norm += x * x;
            norm = std::sqrt(norm);
            const double clip = norm > 5.0 ? 5.0 / norm : 1.0;
            ++step;
            const double c1 = 1.0 - std::pow(b1, double(step)), c2 = 1.0 - std::pow(b2, double(step));
            for (std::size_t i = 0; i < kParams; ++i) {
                const double gi = g[i] * clip;
                m[i] = b1 * m[i] + (1.0 - b1) * gi;
                v[i] = b2 * v[i] + (1.0 - b2) * gi * gi;
                w[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + 1e-8);
                g[i] = 0.0;
            }
        }
        const double ap = average_precision(score(to_params(w, scaler, 0.5), val));
        if (ap > best_ap) best_ap = ap, best = w, best_epoch = e;
        if (e % 5 == 0 || e == 1) std::printf("  epoch %3d  loss %.4f  val AP %.4f\n", e, loss / double(train.size()), ap);
        if (e - best_epoch >= 10) {
            std::printf("  early stop at epoch %d\n", e);
            break;
        }
    }
    const double threshold = best_f1_threshold(score(to_params(best, scaler, 0.5), val));
    const ml::LstmParams final_params = to_params(best, scaler, threshold);
    std::printf("best epoch %d (val AP %.4f), validation F1-optimal threshold %.6f\n", best_epoch, best_ap, threshold);

    // Held-out test events, scored after the last input CDM.
    std::vector<Scored> baseline, latest, lstm = score(final_params, test);
    std::size_t test_pos = 0;
    for (const auto& s : test) {
        test_pos += s.label;
        baseline.push_back({-s.cdms.back().miss_distance_m, s.label});
        latest.push_back({s.latest_label ? 1.0 : 0.0, s.label});
    }
    std::printf("\nHELD-OUT TEST EVENTS: forecast final-CDM risk from history >= 2 days before TCA  (%zu events, %zu positive)\n",
                test.size(), test_pos);
    std::printf("  %-44s %6s %5s %5s %5s %6s %6s %6s\n", "model", "AP", "TP", "FP", "FN", "prec", "recall", "F1");
    print_row("baseline: last miss distance < 1 km", baseline, -1000.0);
    print_row("LSTM over CDM history (validation threshold)", lstm, threshold);
    print_row("reference: latest log10 Pc >= -6 (uses Pc)", latest, 0.5);

    std::ostringstream comment;
    comment << "OCAS LSTM(16) final-risk forecaster, trained by tools/train_lstm\n"
            << "Data: ESA Kelvins train file, event-level split 70/15/15 (fixed), init/shuffle seed " << seed
            << ", inputs t_TCA >= 2 days\n"
            << "Label: final CDM log10 Pc >= -6 (ESA challenge definition) | pos_weight " << pos_weight
            << " | best epoch " << best_epoch << "\n"
            << "Threshold: F1-optimal on validation events | validation AP " << best_ap;
    const std::string out = root + "/models/lstm_weights.txt";
    std::string err;
    ml::LstmParams check{};
    if (!ml::save_lstm(out, final_params, comment.str()) || !ml::load_lstm(out, check, err)) {
        std::fprintf(stderr, "cannot write %s %s\n", out.c_str(), err.c_str());
        return 1;
    }
    std::printf("\nsaved %s (reload verified)\n", out.c_str());
    return 0;
}
