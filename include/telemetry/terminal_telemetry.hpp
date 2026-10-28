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
};

Ansi make_ansi(bool enabled) noexcept;

// Prints the first kRows advisories (none when quiet); the audit log has all of them.
class TerminalTelemetry final : public IPipelineObserver {
public:
    static constexpr std::uint64_t kRows = 10;

    TerminalTelemetry(bool color, bool quiet) noexcept : a_(make_ansi(color)), quiet_(quiet) {}

    void on_session_start(const SessionInfo& s) override;
    void on_advisory(const LedgerRecord& r) override;
    void on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) override;

    [[nodiscard]] std::uint64_t advisories_hidden() const noexcept { return seen_ - printed_; }

private:
    Ansi a_;
    bool quiet_;
    std::uint64_t seen_ = 0;
    std::uint64_t printed_ = 0;
};

}  // namespace ocas

#endif
