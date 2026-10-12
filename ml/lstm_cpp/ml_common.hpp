// ============================================================================
// OCAS offline ML toolkit: shared helpers for train_lstm.
// Ground tools only: they allocate freely and are never linked into flight code.
// The flight-side forward passes in include/ml/neural.hpp are reused for every
// evaluation, so reported metrics are those of the code that actually flies.
// ============================================================================
#ifndef OCAS_TOOLS_ML_COMMON_HPP
#define OCAS_TOOLS_ML_COMMON_HPP

#include "core/types.hpp"
#include "ml/neural.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace ocas::mltools {

/// One CDM from data/esa/train_clean.csv (output of tools/clean_esa_dataset).
struct Cdm {
    std::uint32_t event = 0;
    double t = 0.0;        ///< time_to_tca_sec
    double d = 0.0;        ///< miss_distance_m
    double v = 0.0;        ///< relative speed [km/s]
    double cov = 0.0;      ///< combined RTN covariance trace [m^2]
    bool label = false;    ///< is_actionable: provider Pc > 1e-4
};

inline ConjunctionTelemetry to_telemetry(const Cdm& c) {
    ConjunctionTelemetry t{};
    t.alert_id = c.event;
    t.miss_distance_m = c.d;
    t.relative_speed_km_s = c.v;
    t.time_to_tca_sec = c.t;
    t.cov_trace_m2 = c.cov;
    return t;
}

/// Same domain the flight RiskClassifier sends to a trained model.
inline bool in_model_domain(const Cdm& c) {
    return std::isfinite(c.d) && c.d >= 0.0 && std::isfinite(c.v) && c.v >= 0.0 && c.v <= 17.5 &&
           covariance_usable(c.cov);
}

inline bool load_esa_clean(const std::string& path, std::vector<Cdm>& out) {
    std::ifstream in(path);
    std::string line;
    if (!in || !std::getline(in, line)) {
        std::cerr << "cannot read " << path << " (generate it with tools/clean_esa_dataset)\n";
        return false;
    }
    std::vector<std::string> h;
    {
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) h.push_back(c);
    }
    auto col = [&](const char* n) {
        for (std::size_t i = 0; i < h.size(); ++i) if (h[i] == n) return static_cast<int>(i);
        return -1;
    };
    const int ce = col("event_id"), ct = col("time_to_tca_sec"), cd = col("miss_distance_m"),
              cv = col("relative_speed_ms"), cl = col("is_actionable");
    const int cs[6] = {col("t_sigma_r_m"), col("t_sigma_t_m"), col("t_sigma_n_m"),
                       col("c_sigma_r_m"), col("c_sigma_t_m"), col("c_sigma_n_m")};
    for (int c : {ce, ct, cd, cv, cl, cs[0], cs[1], cs[2], cs[3], cs[4], cs[5]}) {
        if (c < 0) { std::cerr << path << ": unexpected header\n"; return false; }
    }
    std::vector<double> f(h.size());
    while (std::getline(in, line)) {
        std::size_t i = 0;
        const char* p = line.c_str();
        while (i < f.size()) {
            char* end = nullptr;
            f[i++] = std::strtod(p, &end);
            p = end;
            if (*p != ',') break;
            ++p;
        }
        if (i < f.size()) continue;
        Cdm c;
        c.event = static_cast<std::uint32_t>(f[ce]);
        c.t = f[ct];
        c.d = f[cd];
        c.v = f[cv] / 1000.0;
        c.cov = 0.0;
        for (int k : cs) c.cov += f[k] * f[k];
        c.label = f[cl] > 0.5;
        out.push_back(c);
    }
    return true;
}

// ---- Deterministic RNG and event-level split ---------------------------------
struct SplitMix64 {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) { return lo + (hi - lo) * (static_cast<double>(next() >> 11) * 0x1.0p-53); }
    template <typename T>
    void shuffle(std::vector<T>& v) {
        for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[next() % i]);
    }
};

enum class Split { Train, Val, Test };

/// Split by EVENT, never by CDM: CDMs of one event are highly correlated, so a
/// per-row split would leak test information into training.
inline Split split_of(std::uint32_t event) {
    SplitMix64 r{0xC0FFEEull ^ event};
    const auto bucket = r.next() % 100;
    return bucket < 70 ? Split::Train : bucket < 85 ? Split::Val : Split::Test;
}

// ---- Optimiser ----------------------------------------------------------------
struct Adam {
    double lr = 1e-3, b1 = 0.9, b2 = 0.999, eps = 1e-8;
    std::vector<double> m, v;
    long t = 0;
    void step(std::vector<double>& w, std::vector<double>& g, double clip_norm) {
        if (m.empty()) { m.assign(w.size(), 0.0); v.assign(w.size(), 0.0); }
        double norm = 0.0;
        for (double x : g) norm += x * x;
        norm = std::sqrt(norm);
        const double scale = (clip_norm > 0.0 && norm > clip_norm) ? clip_norm / norm : 1.0;
        ++t;
        const double c1 = 1.0 - std::pow(b1, static_cast<double>(t));
        const double c2 = 1.0 - std::pow(b2, static_cast<double>(t));
        for (std::size_t i = 0; i < w.size(); ++i) {
            const double gi = g[i] * scale;
            m[i] = b1 * m[i] + (1.0 - b1) * gi;
            v[i] = b2 * v[i] + (1.0 - b2) * gi * gi;
            w[i] -= lr * (m[i] / c1) / (std::sqrt(v[i] / c2) + eps);
            g[i] = 0.0;
        }
    }
};

// ---- Metrics ------------------------------------------------------------------
struct Scored {
    double score;
    bool label;
};

/// Average precision (area under the precision-recall curve, step-wise).
/// Threshold-free and appropriate for heavy class imbalance.
inline double average_precision(std::vector<Scored> s) {
    std::sort(s.begin(), s.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    std::size_t pos = 0;
    for (const auto& x : s) pos += x.label;
    if (pos == 0) return 0.0;
    double ap = 0.0;
    std::size_t tp = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i].label) {
            ++tp;
            ap += static_cast<double>(tp) / static_cast<double>(i + 1);
        }
    }
    return ap / static_cast<double>(pos);
}

struct Confusion {
    std::size_t tp = 0, fp = 0, fn = 0, tn = 0;
    double precision() const { return tp + fp ? double(tp) / double(tp + fp) : 0.0; }
    double recall() const { return tp + fn ? double(tp) / double(tp + fn) : 0.0; }
    double f1() const { const double p = precision(), r = recall(); return p + r > 0 ? 2 * p * r / (p + r) : 0.0; }
    double specificity() const { return tn + fp ? double(tn) / double(tn + fp) : 0.0; }
    double accuracy() const { const std::size_t n = tp + fp + fn + tn; return n ? double(tp + tn) / double(n) : 0.0; }
};

/// ROC-AUC via the rank-sum (Mann-Whitney U) statistic, ties counted as one half.
/// Threshold-free, but optimistic under heavy imbalance: read it next to AP.
inline double roc_auc(std::vector<Scored> s) {
    std::sort(s.begin(), s.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
    double pos = 0.0, rank_sum = 0.0;
    for (std::size_t i = 0; i < s.size();) {
        std::size_t j = i;
        while (j < s.size() && s[j].score == s[i].score) ++j;
        const double avg_rank = 0.5 * double(i + 1 + j);  // mean of ranks i+1 .. j
        for (std::size_t k = i; k < j; ++k) {
            if (s[k].label) { pos += 1.0; rank_sum += avg_rank; }
        }
        i = j;
    }
    const double neg = double(s.size()) - pos;
    if (pos == 0.0 || neg == 0.0) return 0.0;
    return (rank_sum - pos * (pos + 1.0) / 2.0) / (pos * neg);
}

inline Confusion confusion(const std::vector<Scored>& s, double thr) {
    Confusion c;
    for (const auto& x : s) {
        const bool pred = x.score >= thr;
        if (pred && x.label) ++c.tp;
        else if (pred) ++c.fp;
        else if (x.label) ++c.fn;
        else ++c.tn;
    }
    return c;
}

/// Exact F1-optimal threshold over every distinct score (predict positive if score >= thr).
inline double best_f1_threshold(std::vector<Scored> s) {
    std::sort(s.begin(), s.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
    std::size_t pos = 0;
    for (const auto& x : s) pos += x.label;
    double best_f1 = -1.0, best_thr = 1.0;
    std::size_t tp = 0, fp = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i].label ? ++tp : ++fp;
        if (i + 1 < s.size() && s[i + 1].score == s[i].score) continue;
        const double f1 = 2.0 * double(tp) / double(2 * tp + fp + (pos - tp));
        if (f1 > best_f1) { best_f1 = f1; best_thr = s[i].score; }
    }
    return best_thr;
}

inline void print_row(const char* name, const std::vector<Scored>& s, double thr) {
    const Confusion c = confusion(s, thr);
    std::printf("  %-44s %6.3f %6.3f %5zu %5zu %5zu %6zu %8.3f %6.3f %6.3f %6.3f %6.3f\n", name, average_precision(s),
                roc_auc(s), c.tp, c.fp, c.fn, c.tn, c.accuracy(), c.recall(), c.specificity(), c.precision(), c.f1());
}

inline void print_header(const char* title, std::size_t n, std::size_t pos) {
    std::printf("\n%s  (%zu samples, %zu positive)\n", title, n, pos);
    std::printf("  %-44s %6s %6s %5s %5s %5s %6s %8s %6s %6s %6s %6s\n", "model", "AP", "ROCAUC", "TP", "FP", "FN", "TN",
                "accuracy", "sens", "spec", "prec", "F1");
}

inline ml::FeatureScaler fit_scaler(const std::vector<ml::Features>& raw) {
    ml::FeatureScaler s;
    for (std::size_t i = 0; i < ml::kFeatures; ++i) {
        double m = 0.0, q = 0.0;
        for (const auto& r : raw) m += r[i];
        m /= static_cast<double>(raw.size());
        for (const auto& r : raw) q += (r[i] - m) * (r[i] - m);
        s.mean[i] = m;
        s.stddev[i] = std::max(1e-9, std::sqrt(q / static_cast<double>(raw.size())));
    }
    return s;
}

}  // namespace ocas::mltools

#endif  // OCAS_TOOLS_ML_COMMON_HPP
