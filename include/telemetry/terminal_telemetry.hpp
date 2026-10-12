// ============================================================================
// OCAS headless ANSI terminal telemetry (an IPipelineObserver).
// Writes with std::printf to a block-buffered stdout (configured in main), so
// a slow terminal never stalls the cycle on a per-line flush.
// ============================================================================
#ifndef OCAS_TELEMETRY_TERMINAL_TELEMETRY_HPP
#define OCAS_TELEMETRY_TERMINAL_TELEMETRY_HPP

#include "core/observer.hpp"

#include <cstdint>

namespace ocas {

struct Ansi {
    const char* reset;
    const char* bold;
    const char* dim;
    const char* red;
    const char* green;
    const char* yellow;
    const char* cyan;
    const char* magenta;
};

Ansi make_ansi(bool enabled) noexcept;

/// How much the terminal shows. Quiet: header and summary only. Normal: the
/// first kNormalRows advisories in plain columns. Verbose: every advisory with
/// all fields, plus periodic cycle summaries.
enum class Detail : std::uint8_t { Quiet, Normal, Verbose };

class TerminalTelemetry final : public IPipelineObserver {
public:
    static constexpr std::uint64_t kNormalRows = 10;

    TerminalTelemetry(bool color, Detail detail, std::uint64_t summary_every_cycles) noexcept;

    void on_session_start(const SessionInfo& s) override;
    void on_advisory(const LedgerRecord& r) override;
    void on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) override;
    void on_cycle_end(const CycleStats& c) override;

    [[nodiscard]] const Ansi& ansi() const noexcept { return a_; }
    /// Advisories issued but not printed (Normal mode cap).
    [[nodiscard]] std::uint64_t advisories_hidden() const noexcept {
        return advisories_seen_ > advisories_printed_ ? advisories_seen_ - advisories_printed_ : 0;
    }

private:
    Ansi a_;
    Detail detail_;
    std::uint64_t summary_every_;
    std::uint64_t advisories_seen_ = 0;
    std::uint64_t advisories_printed_ = 0;

    void print_verbose_row(const LedgerRecord& r);
    void print_normal_row(const LedgerRecord& r);
};

}  // namespace ocas

#endif  // OCAS_TELEMETRY_TERMINAL_TELEMETRY_HPP
