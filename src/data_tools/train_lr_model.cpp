/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Logistic Regression Weight Trainer — Ground Station Tool
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 *
 * PURPOSE:
 *   Trains the 5 weights of LogisticRiskClassifier (w_bias, w_dist, w_vel,
 *   w_tca, w_cov) using 162,634 real ESA CDM records via mini-batch gradient
 *   descent with class-weighted binary cross-entropy loss.
 *
 * INPUTS:
 *   data/train_clean.csv  (output of clean_esa_dataset)
 *   data/test_clean.csv
 *
 * OUTPUTS:
 *   data/trained_weights.txt       — machine-readable weight file
 *   Console: epoch loss, precision, recall, F1, confusion matrix
 *   Console: C++ patch to paste into include/risk_engine.hpp
 *
 * TRAINING ALGORITHM:
 *   Mini-batch SGD (batch=512), 300 epochs
 *   Loss: class-weighted BCE  (ALERT class weighted 226x)
 *   LR schedule: 0.05, decay x0.95 every 50 epochs
 *   Shuffle: Fisher-Yates with fixed seed 42 (reproducible)
 *
 * FEATURE ENGINEERING (matches LogisticRiskClassifier::evaluate() exactly):
 *   sigma_eff = sqrt( sum of all 6 covariance sigmas squared )
 *   f_d      = 1 - d / (d + REF_DIST_M)            [miss distance factor]
 *   f_v      = v / (v + REF_VEL_KMS)               [velocity factor]
 *   f_t      = exp(-t / REF_TAU_S)                 [urgency factor]
 *   f_sigma  = sigma_eff / (sigma_eff + REF_SIG_M) [uncertainty factor]
 *   logit    = w_bias + w_d*f_d + w_v*f_v + w_t*f_t + w_cov*f_sigma
 *   P_col    = sigmoid(logit)
 * ============================================================================
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Reference normalization constants (physically grounded from data statistics)
// These are FIXED — only the 5 weights are trained.
//
//  REF_DIST_M   = 1000 m   : 1 km = industry "yellow zone" threshold
//  REF_VEL_KMS  = 12.26    : p50 of relative speed in dataset (LEO typical)
//  REF_TAU_S    = 285689   : p50 of time-to-TCA (~3.3 days)
//  REF_SIGMA_M  = 4256     : p50 of combined position uncertainty
// ============================================================================
static constexpr double REF_DIST_M  = 1000.0;
static constexpr double REF_VEL_KMS = 12.26;
static constexpr double REF_TAU_S   = 285689.0;
static constexpr double REF_SIGMA_M = 4256.0;

// Training hyperparameters
static constexpr int    EPOCHS       = 300;
static constexpr int    BATCH_SIZE   = 512;
static constexpr double LR_INITIAL   = 0.05;
static constexpr double LR_DECAY     = 0.95;
static constexpr int    LR_STEP      = 50;
static constexpr double CLASS_WEIGHT = 226.0; // SAFE:ALERT ratio from data
static constexpr int    RANDOM_SEED  = 42;

// ============================================================================
// Data record after feature engineering
// ============================================================================
struct TrainRecord {
    double f_d;
    double f_v;
    double f_t;
    double f_sigma;
    int    label;
};

// ============================================================================
// CSV helpers
// ============================================================================
static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string field;
    for (char c : line) {
        if (c == ',') { out.push_back(field); field.clear(); }
        else if (c != '\r') { field += c; }
    }
    out.push_back(field);
    return out;
}

static double to_d(const std::string& s) {
    if (s.empty()) return 0.0;
    try { return std::stod(s); } catch (...) { return 0.0; }
}

// ============================================================================
// Load + engineer features from cleaned CSV
// ============================================================================
static std::vector<TrainRecord> load_dataset(const std::string& path,
                                              int& out_alert, int& out_safe) {
    std::ifstream f(path);
    if (!f.is_open()) {
        std::cerr << "[ERROR] Cannot open: " << path << "\n";
        return {};
    }

    std::string header_line;
    std::getline(f, header_line);
    auto hdr = split_csv(header_line);

    auto col = [&](const std::string& name) -> int {
        for (int i = 0; i < (int)hdr.size(); ++i)
            if (hdr[i] == name) return i;
        return -1;
    };

    int idx_miss  = col("miss_distance_m");
    int idx_vel   = col("relative_speed_ms");
    int idx_tca   = col("time_to_tca_sec");
    int idx_tsr   = col("t_sigma_r_m");
    int idx_tst   = col("t_sigma_t_m");
    int idx_tsn   = col("t_sigma_n_m");
    int idx_csr   = col("c_sigma_r_m");
    int idx_cst   = col("c_sigma_t_m");
    int idx_csn   = col("c_sigma_n_m");
    int idx_label = col("is_actionable");

    if (idx_miss < 0 || idx_vel < 0 || idx_tca < 0 || idx_label < 0) {
        std::cerr << "[ERROR] Missing required columns in: " << path << "\n";
        return {};
    }

    std::vector<TrainRecord> recs;
    recs.reserve(180000);
    out_alert = 0; out_safe = 0;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '\r') continue;
        auto fields = split_csv(line);
        if ((int)fields.size() <= idx_label) continue;

        double d_m  = std::max(0.0, to_d(fields[idx_miss]));
        double v_ms = std::max(0.0, to_d(fields[idx_vel]));
        double t_s  = std::max(0.0, to_d(fields[idx_tca]));
        int label   = (to_d(fields[idx_label]) > 0.5) ? 1 : 0;

        double tr = (idx_tsr >= 0) ? to_d(fields[idx_tsr]) : 0.0;
        double tt = (idx_tst >= 0) ? to_d(fields[idx_tst]) : 0.0;
        double tn = (idx_tsn >= 0) ? to_d(fields[idx_tsn]) : 0.0;
        double cr = (idx_csr >= 0) ? to_d(fields[idx_csr]) : 0.0;
        double ct = (idx_cst >= 0) ? to_d(fields[idx_cst]) : 0.0;
        double cn = (idx_csn >= 0) ? to_d(fields[idx_csn]) : 0.0;
        double sigma_eff = std::sqrt(tr*tr + tt*tt + tn*tn + cr*cr + ct*ct + cn*cn);

        double v_kms   = v_ms / 1000.0;
        double f_d     = 1.0 - (d_m    / (d_m    + REF_DIST_M));
        double f_v     = v_kms / (v_kms  + REF_VEL_KMS);
        double f_t     = std::exp(-t_s  / REF_TAU_S);
        double f_sigma = sigma_eff / (sigma_eff + REF_SIGMA_M);

        recs.push_back({f_d, f_v, f_t, f_sigma, label});
        if (label == 1) ++out_alert; else ++out_safe;
    }
    return recs;
}

// ============================================================================
// Sigmoid with overflow protection
// ============================================================================
static inline double sigmoid(double z) {
    z = std::clamp(z, -30.0, 30.0);
    return 1.0 / (1.0 + std::exp(-z));
}

// ============================================================================
// Class-weighted BCE loss for one sample
// ============================================================================
static inline double bce_loss(double p, int y, double w_pos) {
    static constexpr double EPS = 1e-12;
    double w = (y == 1) ? w_pos : 1.0;
    return -w * ((double)y * std::log(p + EPS) + (1.0 - y) * std::log(1.0 - p + EPS));
}

// ============================================================================
// Evaluation metrics
// ============================================================================
struct Metrics {
    double loss, accuracy, precision, recall, f1;
    int TP, FP, TN, FN;
};

static Metrics evaluate(const std::vector<TrainRecord>& data,
                        double w_bias, double w_d, double w_v,
                        double w_t, double w_cov,
                        double class_weight, double threshold) {
    double total_loss = 0.0;
    int TP = 0, FP = 0, TN = 0, FN = 0;

    for (const auto& r : data) {
        double logit = w_bias + w_d*r.f_d + w_v*r.f_v + w_t*r.f_t + w_cov*r.f_sigma;
        double p     = sigmoid(logit);
        total_loss  += bce_loss(p, r.label, class_weight);
        int pred     = (p >= threshold) ? 1 : 0;
        if      (r.label == 1 && pred == 1) ++TP;
        else if (r.label == 0 && pred == 1) ++FP;
        else if (r.label == 0 && pred == 0) ++TN;
        else                                ++FN;
    }

    double prec = (TP + FP > 0) ? (double)TP / (TP + FP) : 0.0;
    double rec  = (TP + FN > 0) ? (double)TP / (TP + FN) : 0.0;
    double f1   = (prec + rec  > 0) ? 2.0 * prec * rec / (prec + rec) : 0.0;
    double acc  = (double)(TP + TN) / (double)data.size();

    return { total_loss / (double)data.size(), acc, prec, rec, f1, TP, FP, TN, FN };
}

// ============================================================================
// MAIN
// ============================================================================
int main(int argc, char* argv[]) {
    const std::string base       = (argc > 1) ? std::string(argv[1]) : ".";
    const std::string train_path = base + "/data/train_clean.csv";
    const std::string test_path  = base + "/data/test_clean.csv";
    const std::string out_path   = base + "/data/trained_weights.txt";

    std::cout << "================================================================\n"
              << "  OCAS Logistic Regression Trainer\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "================================================================\n\n";

    // --- 1. LOAD ---
    std::cout << "[1/5] Loading datasets...\n";
    int tr_alert = 0, tr_safe = 0, te_alert = 0, te_safe = 0;
    auto train_data = load_dataset(train_path, tr_alert, tr_safe);
    auto test_data  = load_dataset(test_path,  te_alert, te_safe);

    if (train_data.empty()) {
        std::cerr << "[FATAL] Training data failed to load. Run make clean_data first.\n";
        return 1;
    }

    std::cout << "  Train : " << train_data.size() << " rows"
              << "  ALERT=" << tr_alert << "  SAFE=" << tr_safe
              << "  ratio=" << std::fixed << std::setprecision(1)
              << (double)tr_safe / std::max(1, tr_alert) << "x\n";
    std::cout << "  Test  : " << test_data.size()  << " rows"
              << "  ALERT=" << te_alert << "  SAFE=" << te_safe << "\n";

    // --- 2. INIT WEIGHTS ---
    std::cout << "\n[2/5] Initialising weights to zero...\n\n";
    double w_bias = 0.0, w_d = 0.0, w_v = 0.0, w_t = 0.0, w_cov = 0.0;

    // --- 3. TRAIN ---
    std::cout << "[3/5] Mini-batch SGD  epochs=" << EPOCHS
              << "  batch=" << BATCH_SIZE
              << "  lr=" << LR_INITIAL
              << "  class_weight=" << (int)CLASS_WEIGHT << "x\n\n";

    std::cout << std::setw(8)  << "Epoch"
              << std::setw(16) << "Train Loss"
              << std::setw(12) << "LR\n"
              << std::string(36, '-') << "\n";

    std::mt19937 rng(RANDOM_SEED);
    std::vector<int> indices(train_data.size());
    std::iota(indices.begin(), indices.end(), 0);
    double lr = LR_INITIAL;

    for (int epoch = 0; epoch < EPOCHS; ++epoch) {
        if (epoch > 0 && epoch % LR_STEP == 0) lr *= LR_DECAY;
        std::shuffle(indices.begin(), indices.end(), rng);

        double epoch_loss = 0.0;
        int batches = 0;

        for (int start = 0; start < (int)train_data.size(); start += BATCH_SIZE) {
            int end = std::min(start + BATCH_SIZE, (int)train_data.size());
            int n   = end - start;

            double gb = 0.0, gd = 0.0, gv = 0.0, gt = 0.0, gc = 0.0;
            double bloss = 0.0;

            for (int i = start; i < end; ++i) {
                const auto& r = train_data[indices[i]];
                double logit  = w_bias + w_d*r.f_d + w_v*r.f_v + w_t*r.f_t + w_cov*r.f_sigma;
                double p      = sigmoid(logit);
                bloss        += bce_loss(p, r.label, CLASS_WEIGHT);
                double ws     = (r.label == 1) ? CLASS_WEIGHT : 1.0;
                double delta  = ws * (p - r.label);
                gb += delta;
                gd += delta * r.f_d;
                gv += delta * r.f_v;
                gt += delta * r.f_t;
                gc += delta * r.f_sigma;
            }

            double inv = lr / n;
            w_bias -= inv * gb;
            w_d    -= inv * gd;
            w_v    -= inv * gv;
            w_t    -= inv * gt;
            w_cov  -= inv * gc;

            epoch_loss += bloss / n;
            ++batches;
        }

        epoch_loss /= batches;
        if (epoch == 0 || (epoch + 1) % 10 == 0 || epoch == EPOCHS - 1) {
            std::cout << std::setw(8)  << (epoch + 1)
                      << std::setw(16) << std::fixed << std::setprecision(6) << epoch_loss
                      << std::setw(12) << std::scientific << std::setprecision(3) << lr << "\n";
        }
    }

    // --- 4. THRESHOLD SWEEP + EVALUATE ---
    std::cout << "\n[4/5] Sweeping threshold for best F1...\n";
    double best_f1 = -1.0, best_thresh = 0.5;
    for (int ti = 1; ti < 100; ++ti) {
        double th = ti * 0.01;
        auto m = evaluate(train_data, w_bias, w_d, w_v, w_t, w_cov, CLASS_WEIGHT, th);
        if (m.f1 > best_f1) { best_f1 = m.f1; best_thresh = th; }
    }
    std::cout << "  Best threshold: " << best_thresh
              << "  Best F1 (train): " << std::fixed << std::setprecision(4) << best_f1 * 100 << " %\n\n";

    auto print_metrics = [&](const char* label, const Metrics& m) {
        std::cout << "  ---- " << label << " (threshold=" << best_thresh << ") ----\n";
        std::cout << "  Loss      : " << std::fixed << std::setprecision(6) << m.loss << "\n";
        std::cout << "  Accuracy  : " << std::setprecision(4) << m.accuracy  * 100 << " %\n";
        std::cout << "  Precision : " << std::setprecision(4) << m.precision * 100 << " %\n";
        std::cout << "  Recall    : " << std::setprecision(4) << m.recall    * 100 << " %  <- catch rate for real ALERTs\n";
        std::cout << "  F1 Score  : " << std::setprecision(4) << m.f1        * 100 << " %\n";
        std::cout << "  Confusion Matrix:\n";
        std::cout << "                 Pred SAFE   Pred ALERT\n";
        std::cout << "  Actual SAFE  : " << std::setw(10) << m.TN << "   " << std::setw(10) << m.FP << "\n";
        std::cout << "  Actual ALERT : " << std::setw(10) << m.FN << "   " << std::setw(10) << m.TP << "\n\n";
    };

    auto train_m = evaluate(train_data, w_bias, w_d, w_v, w_t, w_cov, CLASS_WEIGHT, best_thresh);
    auto test_m  = evaluate(test_data,  w_bias, w_d, w_v, w_t, w_cov, CLASS_WEIGHT, best_thresh);
    print_metrics("TRAIN SET", train_m);
    print_metrics("TEST SET",  test_m);

    // --- 5. SAVE + PATCH ---
    std::cout << "[5/5] Saving weights...\n";
    std::ofstream wf(out_path);
    if (wf.is_open()) {
        wf << std::fixed << std::setprecision(10);
        wf << "# OCAS Trained Logistic Regression Weights\n";
        wf << "# Team: DSCPP-III-2026-T018 | Graphic Era University\n";
        wf << "# Source: ESA Kelvins CA Challenge (162,634 CDM records)\n";
        wf << "# Method: Mini-batch SGD | Epochs=" << EPOCHS
           << " | Batch=" << BATCH_SIZE
           << " | ClassWeight=" << (int)CLASS_WEIGHT << "\n";
        wf << "# Optimal threshold=" << best_thresh
           << " | F1(test)=" << std::setprecision(4) << test_m.f1 << "\n#\n";
        wf << "w_bias      " << w_bias       << "\n";
        wf << "w_dist      " << w_d          << "\n";
        wf << "w_vel       " << w_v          << "\n";
        wf << "w_tca       " << w_t          << "\n";
        wf << "w_cov       " << w_cov        << "\n";
        wf << "ref_dist_m  " << REF_DIST_M   << "\n";
        wf << "ref_vel_kms " << REF_VEL_KMS  << "\n";
        wf << "ref_tau_s   " << REF_TAU_S    << "\n";
        wf << "ref_sigma_m " << REF_SIGMA_M  << "\n";
        wf.close();
        std::cout << "  Saved -> " << out_path << "\n\n";
    }

    std::cout << "================================================================\n";
    std::cout << "  PASTE INTO include/risk_engine.hpp\n";
    std::cout << "  (Replace RiskModelHyperparameters struct default values)\n";
    std::cout << "================================================================\n\n";
    std::cout << std::fixed << std::setprecision(10);
    std::cout << "struct RiskModelHyperparameters {\n";
    std::cout << "    double w_bias        = " << w_bias      << "; // trained intercept\n";
    std::cout << "    double w_dist        = " << w_d         << "; // miss distance weight\n";
    std::cout << "    double w_vel         = " << w_v         << "; // relative velocity weight\n";
    std::cout << "    double w_tca         = " << w_t         << "; // temporal urgency weight\n";
    std::cout << "    double w_cov         = " << w_cov       << "; // covariance weight\n";
    std::cout << "    double ref_dist_m    = " << REF_DIST_M  << "; // 1 km yellow-zone threshold\n";
    std::cout << "    double ref_vel_kms   = " << REF_VEL_KMS << "; // p50 LEO relative speed\n";
    std::cout << "    double ref_tau_tca_s = " << REF_TAU_S   << "; // p50 time-to-TCA (~3.3 days)\n";
    std::cout << "    double ref_sigma_m   = " << REF_SIGMA_M << "; // p50 combined sigma\n";
    std::cout << "};\n\n";

    std::cout << "================================================================\n";
    std::cout << "  TRAINING COMPLETE\n";
    std::cout << "  F1(test)=" << std::setprecision(2) << test_m.f1*100 << "%"
              << "  Recall(test)=" << test_m.recall*100 << "%\n";
    std::cout << "================================================================\n";
    return 0;
}
