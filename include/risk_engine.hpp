/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Risk Engine & Max-Heap Priority Queue (C++17)
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Features:
 *  - Deterministic, explainable in-place Logistic Regression model
 *  - Encapsulated MaxHeapPriorityQueue (Binary Max-Heap) for ThreatAlert
 *  - Zero dynamic heap allocation, O(1) ML evaluation, O(log n) threat priority
 * ============================================================================
 */

#ifndef OCAS_RISK_ENGINE_HPP
#define OCAS_RISK_ENGINE_HPP

#include "c_ingestion.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

namespace ocas {

/**
 * @brief Categorical risk classification levels.
 */
enum class RiskLevel : uint8_t {
    LOW = 0,
    MODERATE = 1,
    HIGH = 2,
    CRITICAL = 3
};

/**
 * @brief Recommended tactical flight software action.
 */
enum class TacticalAction : uint8_t {
    NOMINAL_PASS = 0,
    MONITOR_TRACKING = 1,
    PREPARE_MANEUVER_WINDOW = 2,
    IMMEDIATE_AVOIDANCE_BURN = 3
};

/**
 * @brief Transparent explainability breakdown for the edge ML model.
 */
struct ExplainabilityReport {
    double f_miss_distance = 0.0;      /**< Normalized miss distance factor [0, 1] */
    double f_rel_velocity = 0.0;       /**< Normalized relative velocity factor [0, 1] */
    double f_time_to_tca = 0.0;        /**< Normalized temporal urgency factor [0, 1] */
    double f_covariance_trace = 0.0;   /**< Normalized positional uncertainty factor [0, 1] */
    double sigma_eff_m = 0.0;          /**< Effective 1-sigma uncertainty radius [m] */
    double contrib_d = 0.0;            /**< Weighted contribution: w_d * f_d */
    double contrib_v = 0.0;            /**< Weighted contribution: w_v * f_v */
    double contrib_t = 0.0;            /**< Weighted contribution: w_t * f_t */
    double contrib_sigma = 0.0;        /**< Weighted contribution: w_sigma * f_sigma */
    double logit_z = 0.0;              /**< Total log-odds logit value */
    double p_collision = 0.0;          /**< Final calibrated collision probability [0, 1] */
    RiskLevel risk_level = RiskLevel::LOW;        /**< Discrete risk categorization */
    TacticalAction action = TacticalAction::NOMINAL_PASS; /**< Recommended flight action */
};

/**
 * @brief Complete threat record stored in the priority queue.
 */
struct ThreatAlert {
    CDMAlert alert{};
    double risk_score = 0.0;           /**< Collision probability P in [0, 1] */
    ExplainabilityReport report{};     /**< In-depth transparent telemetry report */
    uint64_t evaluated_epoch_s = 0;    /**< Processing timestamp */

    bool operator<(const ThreatAlert& other) const noexcept {
        return risk_score < other.risk_score;
    }
    bool operator>(const ThreatAlert& other) const noexcept {
        return risk_score > other.risk_score;
    }
};

// Backward-compatible alias
using ScoredThreat = ThreatAlert;

/**
 * @brief Edge ML Logistic Regression Model Hyperparameters.
 */
struct RiskModelHyperparameters {
    // -----------------------------------------------------------------------
    // Weights trained via mini-batch SGD on ESA Kelvins CA Challenge dataset
    // 162,634 CDM records | 300 epochs | batch=512 | class_weight=226x
    // Team ID: DSCPP-III-2026-T018 | Graphic Era University
    // -----------------------------------------------------------------------
    double w_bias        = -0.1221970958; /**< Trained intercept */
    double w_dist        =  8.9519815856; /**< Miss distance weight (dominant signal) */
    double w_vel         = -2.1527299613; /**< Relative velocity weight */
    double w_tca         = -4.5139313277; /**< Temporal urgency weight */
    double w_cov         = -0.3941279100; /**< Covariance uncertainty weight */
    // -----------------------------------------------------------------------
    // Reference normalization constants (data-grounded, not trained)
    // ref_dist_m   = 1000 m      : industry 1-km yellow-zone threshold
    // ref_vel_kms  = 12.26 km/s  : p50 LEO relative speed (dataset)
    // ref_tau_tca_s= 285689 s    : p50 time-to-TCA (~3.3 days, dataset)
    // ref_sigma_m  = 4256 m      : p50 combined position uncertainty (dataset)
    // -----------------------------------------------------------------------
    double ref_dist_m    = 1000.0;    /**< Miss distance ref [m] */
    double ref_vel_kms   =   12.26;   /**< Velocity ref [km/s] */
    double ref_tau_tca_s = 285689.0;  /**< TCA decay constant [s] */
    double ref_sigma_m   = 4256.0;    /**< Uncertainty ref [m] */
};

/**
 * @brief In-Place Explainable Logistic Regression Model.
 * Strictly O(1) execution time, constant memory footprint, deterministic arithmetic.
 */
class LogisticRiskClassifier {
public:
    using ModelHyperparameters = RiskModelHyperparameters;

    LogisticRiskClassifier() noexcept;
    explicit LogisticRiskClassifier(const ModelHyperparameters& params) noexcept;

    /**
     * @brief Computes collision probability and explainability telemetry in constant time O(1).
     */
    ThreatAlert evaluate(const CDMAlert& alert, uint64_t current_epoch_s = 0) const noexcept;

    static std::string_view risk_level_to_string(RiskLevel level) noexcept;
    static std::string_view action_to_string(TacticalAction action) noexcept;

private:
    ModelHyperparameters params_;
};

/**
 * @brief Encapsulated Binary Max-Heap Priority Queue.
 * Built directly from a fixed-size raw array for zero-heap embedded predictability.
 * Root always holds the threat with highest risk score P(collision).
 */
class MaxHeapPriorityQueue {
public:
    static constexpr size_t MAX_CAPACITY = 64;

    MaxHeapPriorityQueue() noexcept;
    ~MaxHeapPriorityQueue() = default;

    // Disallow copies for memory and state integrity
    MaxHeapPriorityQueue(const MaxHeapPriorityQueue&) = delete;
    MaxHeapPriorityQueue& operator=(const MaxHeapPriorityQueue&) = delete;
    MaxHeapPriorityQueue(MaxHeapPriorityQueue&&) noexcept = default;
    MaxHeapPriorityQueue& operator=(MaxHeapPriorityQueue&&) noexcept = default;

    /**
     * @brief Insert threat alert into max-heap. Complexity: O(log n).
     * @return true if inserted, false if heap capacity reached.
     */
    bool insert(const ThreatAlert& alert) noexcept;

    /**
     * @brief Extract highest-risk threat alert from root. Complexity: O(log n).
     */
    ThreatAlert extract_max();
    bool extract_max(ThreatAlert& out_alert) noexcept;

    /**
     * @brief Peek at the highest-risk threat. Complexity: O(1).
     */
    [[nodiscard]] const ThreatAlert& peek() const;
    bool peek(ThreatAlert& out_alert) const noexcept;
    bool peek_max(ThreatAlert& out_alert) const noexcept { return peek(out_alert); }

    [[nodiscard]] size_t size() const noexcept { return count_; }
    [[nodiscard]] static constexpr size_t capacity() noexcept { return MAX_CAPACITY; }
    [[nodiscard]] bool is_empty() const noexcept { return count_ == 0; }
    [[nodiscard]] bool is_full() const noexcept { return count_ >= MAX_CAPACITY; }
    void clear() noexcept { count_ = 0; }

    /**
     * @brief Direct read-only pointer access for telemetry and visualizer introspection.
     */
    [[nodiscard]] const ThreatAlert* data() const noexcept { return heap_array_; }

private:
    void sift_up(size_t index) noexcept;
    void sift_down(size_t index) noexcept;

    ThreatAlert heap_array_[MAX_CAPACITY];
    size_t count_;
};

// Backward-compatible alias
using BinaryMaxHeap = MaxHeapPriorityQueue;

} // namespace ocas

#endif /* OCAS_RISK_ENGINE_HPP */
