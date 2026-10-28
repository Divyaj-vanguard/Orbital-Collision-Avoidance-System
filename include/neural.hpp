#ifndef OCAS_NEURAL_HPP
#define OCAS_NEURAL_HPP

#include "types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace ocas::ml {

// Shared by the trainer (tools/train_lstm.cpp) and the flight code so features cannot drift.
// Inputs: log10(d+1), v_rel, t_TCA in days, log10(sqrt(TrC)+1). collision_prob is never an input.
inline constexpr std::size_t kFeatures = 4;
inline constexpr std::size_t kLstmH = 16;

using Features = std::array<double, kFeatures>;

struct FeatureScaler {
    Features mean{0.0, 0.0, 0.0, 0.0};
    Features stddev{1.0, 1.0, 1.0, 1.0};
};

inline Features raw_features(const ConjunctionTelemetry& t) noexcept {
    const double d = std::max(0.0, t.miss_distance_m);
    const double sigma = std::sqrt(std::max(0.0, t.cov_trace_m2));
    return {std::log10(d + 1.0), t.relative_speed_km_s, t.time_to_tca_sec / 86400.0, std::log10(sigma + 1.0)};
}

inline Features standardize(const Features& raw, const FeatureScaler& s) noexcept {
    Features x{};
    for (std::size_t i = 0; i < kFeatures; ++i) x[i] = (raw[i] - s.mean[i]) / s.stddev[i];
    return x;
}

inline double sigmoid(double z) noexcept { return 1.0 / (1.0 + std::exp(-std::clamp(z, -30.0, 30.0))); }

// Gate order [i, f, g, o]; logit = w_out . h + b_out.
struct LstmParams {
    FeatureScaler scaler{};
    std::array<double, 4 * kLstmH * kFeatures> W{};
    std::array<double, 4 * kLstmH * kLstmH> U{};
    std::array<double, 4 * kLstmH> b{};
    std::array<double, kLstmH> w_out{};
    double b_out = 0.0;
    double threshold = 0.5;
};

struct LstmState {
    std::array<double, kLstmH> h{};
    std::array<double, kLstmH> c{};
};

inline double lstm_step(const LstmParams& p, const Features& x, LstmState& s) noexcept {
    constexpr std::size_t H = kLstmH;
    std::array<double, 4 * H> gates{};
    for (std::size_t r = 0; r < 4 * H; ++r) {
        double a = p.b[r];
        for (std::size_t i = 0; i < kFeatures; ++i) a += p.W[r * kFeatures + i] * x[i];
        for (std::size_t j = 0; j < H; ++j) a += p.U[r * H + j] * s.h[j];
        gates[r] = a;
    }
    double z = p.b_out;
    for (std::size_t j = 0; j < H; ++j) {
        s.c[j] = sigmoid(gates[H + j]) * s.c[j] + sigmoid(gates[j]) * std::tanh(gates[2 * H + j]);
        s.h[j] = sigmoid(gates[3 * H + j]) * std::tanh(s.c[j]);
        z += p.w_out[j] * s.h[j];
    }
    return z;
}

bool load_lstm(const std::string& path, LstmParams& out, std::string& error);
bool save_lstm(const std::string& path, const LstmParams& p, const std::string& comment);

}  // namespace ocas::ml

#endif
