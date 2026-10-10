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

class TerminalTelemetry final : public IPipelineObserver {
public:
    TerminalTelemetry(bool color, bool show_advisories, std::uint64_t summary_every_cycles) noexcept;

    void on_session_start(const SessionInfo& s) override;
    void on_advisory(const LedgerRecord& r) override;
    void on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) override;
    void on_cycle_end(const CycleStats& c) override;

    [[nodiscard]] const Ansi& ansi() const noexcept { return a_; }

private:
    Ansi a_;
    bool show_advisories_;
    std::uint64_t summary_every_;
    std::uint64_t advisories_printed_ = 0;
};

}  // namespace ocas

#endif  // OCAS_TELEMETRY_TERMINAL_TELEMETRY_HPP
