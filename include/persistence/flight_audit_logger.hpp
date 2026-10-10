// ============================================================================
// OCAS Module 5c: Flight Audit Logger
// ============================================================================
// RAII append-only JSON-lines logger (default logs/flight_audit.log).
//   * Constructor opens in append mode; destructor flushes and closes.
//   * Every record is formatted with vsnprintf into a member buffer, then
//     written with ofstream::write: no std::string, no heap in the loop.
//   * Records longer than the buffer are truncated and counted, never split.
//   * Implements IPipelineObserver so the pipeline does not know about files.
// ============================================================================
#ifndef OCAS_PERSISTENCE_FLIGHT_AUDIT_LOGGER_HPP
#define OCAS_PERSISTENCE_FLIGHT_AUDIT_LOGGER_HPP

#include "core/observer.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace ocas {

class FlightAuditLogger final : public IPipelineObserver {
public:
    /// Opens (creating parent directories) at init time.
    explicit FlightAuditLogger(const std::string& path, bool log_every_threat = false);
    ~FlightAuditLogger() override;
    FlightAuditLogger(const FlightAuditLogger&) = delete;
    FlightAuditLogger& operator=(const FlightAuditLogger&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return out_.is_open(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t records_written() const noexcept { return records_; }
    [[nodiscard]] std::uint64_t records_truncated() const noexcept { return truncated_; }

#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    void log(const char* event, const char* fmt, ...) noexcept;
    void flush() noexcept;

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
    std::uint64_t records_ = 0;
    std::uint64_t truncated_ = 0;
    bool log_every_threat_;
};

}  // namespace ocas

#endif  // OCAS_PERSISTENCE_FLIGHT_AUDIT_LOGGER_HPP
