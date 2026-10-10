// ============================================================================
// OCAS profiling hooks: fixed-bin latency histogram (no heap) and a scoped
// timer. 25 ns bins up to 100 us; samples above that land in an overflow bin
// and are still reflected exactly in max().
// ============================================================================
#ifndef OCAS_CORE_LATENCY_PROFILER_HPP
#define OCAS_CORE_LATENCY_PROFILER_HPP

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ocas {

class LatencyProfiler {
public:
    static constexpr std::uint64_t kBinNs = 25;
    static constexpr std::size_t kBins = 4000;  // 4000 * 25 ns = 100 us

    void record(std::uint64_t ns) noexcept {
        const std::size_t bin = static_cast<std::size_t>(ns / kBinNs);
        ++bins_[bin < kBins ? bin : kBins];
        ++count_;
        sum_ns_ += ns;
        if (count_ == 1 || ns < min_ns_) min_ns_ = ns;
        if (ns > max_ns_) max_ns_ = ns;
    }

    /// Upper edge of the bin containing quantile q in [0, 1], in ns.
    [[nodiscard]] std::uint64_t percentile_ns(double q) const noexcept {
        if (count_ == 0) return 0;
        const auto target = static_cast<std::uint64_t>(q * static_cast<double>(count_ - 1)) + 1;
        std::uint64_t seen = 0;
        for (std::size_t i = 0; i < kBins; ++i) {
            seen += bins_[i];
            if (seen >= target) return (static_cast<std::uint64_t>(i) + 1) * kBinNs;
        }
        return max_ns_;
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] std::uint64_t min_ns() const noexcept { return min_ns_; }
    [[nodiscard]] std::uint64_t max_ns() const noexcept { return max_ns_; }
    [[nodiscard]] double mean_ns() const noexcept {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_ns_) / static_cast<double>(count_);
    }

private:
    std::array<std::uint64_t, kBins + 1> bins_{};
    std::uint64_t count_ = 0;
    std::uint64_t sum_ns_ = 0;
    std::uint64_t min_ns_ = 0;
    std::uint64_t max_ns_ = 0;
};

using SteadyClock = std::chrono::steady_clock;

inline std::uint64_t elapsed_ns(SteadyClock::time_point since) noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(SteadyClock::now() - since).count());
}

}  // namespace ocas

#endif  // OCAS_CORE_LATENCY_PROFILER_HPP
