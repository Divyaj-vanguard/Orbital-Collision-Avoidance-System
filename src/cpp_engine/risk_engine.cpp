/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Flight Software Architecture - Risk Engine & Priority Queue Implementation
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 */

#include "risk_engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace ocas {

LogisticRiskClassifier::LogisticRiskClassifier() noexcept
    : params_{} {}

LogisticRiskClassifier::LogisticRiskClassifier(const ModelHyperparameters& params) noexcept
    : params_(params) {}

ThreatAlert LogisticRiskClassifier::evaluate(const CDMAlert& alert, uint64_t current_epoch_s) const noexcept {
    // 1. Normalized Miss Distance Factor [0, 1] (shorter miss distance -> higher risk)
    const double d_miss = std::max(0.0, alert.d_miss_m);
    const double f_d = 1.0 - (d_miss / (d_miss + params_.ref_dist_m));

    // 2. Normalized Relative Velocity Factor [0, 1] (hypervelocity -> higher kinetic severity)
    const double v_rel = std::max(0.0, alert.v_rel_kms);
    const double f_v = v_rel / (v_rel + params_.ref_vel_kms);

    // 3. Normalized Temporal Urgency Factor [0, 1] (imminent TCA -> exponential urgency)
    const double t_tca = std::max(0.0, alert.t_tca_s);
    const double f_t = std::exp(-t_tca / params_.ref_tau_tca_s);

    // 4. Normalized Covariance Dispersion Factor [0, 1] (trace of positional uncertainty)
    const double cov_trace = std::max(0.0, alert.covariance[0][0] +
                                           alert.covariance[1][1] +
                                           alert.covariance[2][2]);
    const double sigma_eff = std::sqrt(cov_trace);
    const double f_sigma = sigma_eff / (sigma_eff + params_.ref_sigma_m);

    // Explainable additive linear model: logit z
    const double c_d     = params_.w_dist * f_d;
    const double c_v     = params_.w_vel  * f_v;
    const double c_t     = params_.w_tca  * f_t;
    const double c_sigma = params_.w_cov  * f_sigma;

    double logit = params_.w_bias + c_d + c_v + c_t + c_sigma;

    // Numerical protection against floating point overflow
    logit = std::clamp(logit, -30.0, 30.0);

    // Sigmoidal transfer function: P = 1 / (1 + e^-z)
    const double p_col = 1.0 / (1.0 + std::exp(-logit));

    // Determine discrete risk classification & tactical flight action
    RiskLevel r_level = RiskLevel::LOW;
    TacticalAction action = TacticalAction::NOMINAL_PASS;

    if (p_col >= 0.85) {
        r_level = RiskLevel::CRITICAL;
        action  = TacticalAction::IMMEDIATE_AVOIDANCE_BURN;
    } else if (p_col >= 0.50) {
        r_level = RiskLevel::HIGH;
        action  = TacticalAction::PREPARE_MANEUVER_WINDOW;
    } else if (p_col >= 0.20) {
        r_level = RiskLevel::MODERATE;
        action  = TacticalAction::MONITOR_TRACKING;
    }

    ExplainabilityReport report{
        f_d,
        f_v,
        f_t,
        f_sigma,
        sigma_eff,
        c_d,
        c_v,
        c_t,
        c_sigma,
        logit,
        p_col,
        r_level,
        action
    };

    return ThreatAlert{
        alert,
        p_col,
        report,
        current_epoch_s
    };
}

std::string_view LogisticRiskClassifier::risk_level_to_string(RiskLevel level) noexcept {
    switch (level) {
        case RiskLevel::CRITICAL: return "CRITICAL";
        case RiskLevel::HIGH:     return "HIGH";
        case RiskLevel::MODERATE: return "MODERATE";
        case RiskLevel::LOW:      return "LOW";
    }
    return "UNKNOWN";
}

std::string_view LogisticRiskClassifier::action_to_string(TacticalAction action) noexcept {
    switch (action) {
        case TacticalAction::IMMEDIATE_AVOIDANCE_BURN: return "IMMEDIATE_AVOIDANCE_BURN";
        case TacticalAction::PREPARE_MANEUVER_WINDOW:  return "PREPARE_MANEUVER_WINDOW";
        case TacticalAction::MONITOR_TRACKING:         return "MONITOR_TRACKING";
        case TacticalAction::NOMINAL_PASS:             return "NOMINAL_PASS";
    }
    return "UNKNOWN";
}

/* ============================================================================
 * MaxHeapPriorityQueue Implementation
 * ============================================================================ */

MaxHeapPriorityQueue::MaxHeapPriorityQueue() noexcept : count_(0) {}

void MaxHeapPriorityQueue::sift_up(size_t index) noexcept {
    while (index > 0) {
        const size_t parent = (index - 1) / 2;
        if (heap_array_[index].risk_score > heap_array_[parent].risk_score) {
            std::swap(heap_array_[index], heap_array_[parent]);
            index = parent;
        } else {
            break;
        }
    }
}

void MaxHeapPriorityQueue::sift_down(size_t index) noexcept {
    while (true) {
        const size_t left_child = 2 * index + 1;
        const size_t right_child = 2 * index + 2;
        size_t largest = index;

        if (left_child < count_ && heap_array_[left_child].risk_score > heap_array_[largest].risk_score) {
            largest = left_child;
        }
        if (right_child < count_ && heap_array_[right_child].risk_score > heap_array_[largest].risk_score) {
            largest = right_child;
        }

        if (largest != index) {
            std::swap(heap_array_[index], heap_array_[largest]);
            index = largest;
        } else {
            break;
        }
    }
}

bool MaxHeapPriorityQueue::insert(const ThreatAlert& alert) noexcept {
    if (count_ >= MAX_CAPACITY) {
        return false; // Embedded bounds protection
    }

    heap_array_[count_] = alert;
    sift_up(count_);
    count_++;
    return true;
}

const ThreatAlert& MaxHeapPriorityQueue::peek() const {
    if (count_ == 0) {
        throw std::runtime_error("MaxHeapPriorityQueue is empty");
    }
    return heap_array_[0];
}

bool MaxHeapPriorityQueue::peek(ThreatAlert& out_alert) const noexcept {
    if (count_ == 0) {
        return false;
    }
    out_alert = heap_array_[0];
    return true;
}

ThreatAlert MaxHeapPriorityQueue::extract_max() {
    if (count_ == 0) {
        throw std::runtime_error("MaxHeapPriorityQueue is empty");
    }
    ThreatAlert top = heap_array_[0];
    count_--;
    if (count_ > 0) {
        heap_array_[0] = heap_array_[count_];
        sift_down(0);
    }
    return top;
}

bool MaxHeapPriorityQueue::extract_max(ThreatAlert& out_alert) noexcept {
    if (count_ == 0) {
        return false;
    }

    out_alert = heap_array_[0];
    count_--;

    if (count_ > 0) {
        heap_array_[0] = heap_array_[count_];
        sift_down(0);
    }

    return true;
}

} // namespace ocas
