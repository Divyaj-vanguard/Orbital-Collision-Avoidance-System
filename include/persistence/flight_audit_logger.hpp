#ifndef OCAS_PERSISTENCE_FLIGHT_AUDIT_LOGGER_HPP
#define OCAS_PERSISTENCE_FLIGHT_AUDIT_LOGGER_HPP

#include "core/observer.hpp"

#include <array>
#include <fstream>
#include <string>

namespace ocas {

// RAII append-only JSON-lines log. Records are formatted into a member buffer, so
// logging never allocates; a record longer than the buffer is truncated.
class FlightAuditLogger final : public IPipelineObserver {
public:
    explicit FlightAuditLogger(const std::string& path, bool log_every_threat = false);
    FlightAuditLogger(const FlightAuditLogger&) = delete;
    FlightAuditLogger& operator=(const FlightAuditLogger&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return out_.is_open(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    void log(const char* event, const char* fmt, ...) noexcept;

    void on_session_start(const SessionInfo& s) override;
    void on_input_rejected(const ConjunctionTelemetry& t, InputRejectReason r) override;
    void on_threat_scored(const ScoredThreat& s) override;
    void on_advisory(const LedgerRecord& r) override;
    void on_advisory_resolved(const LedgerRecord& r) override;
    void on_rollback(std::uint32_t alert_id, std::size_t unwound, const SpacecraftState& s) override;
    void on_cycle_end(const CycleStats& c) override;
    void on_session_end(const CycleStats& totals) override;

private:
    void log_record(const char* event, const LedgerRecord& r) noexcept;

    std::string path_;
    std::ofstream out_;
    std::array<char, 1024> line_{};
    bool log_every_threat_;
};

}  // namespace ocas

#endif
