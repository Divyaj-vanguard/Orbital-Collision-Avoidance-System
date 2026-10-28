#ifndef OCAS_RECORDER_HPP
#define OCAS_RECORDER_HPP

#include "regime.hpp"
#include "types.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

namespace ocas {

struct Ansi {
    const char *reset, *bold, *dim, *red, *green, *yellow, *cyan;
};

Ansi make_ansi(bool enabled) noexcept;

// Writes every pipeline event to an append-only JSON-lines audit log and prints the first
// kRows advisories to the terminal (none when quiet). Lines are formatted into a member
// buffer, so recording never allocates; a line longer than the buffer is truncated.
class Recorder {
public:
    static constexpr std::uint64_t kRows = 10;

    Recorder(const std::string& path, bool log_every_threat, bool quiet, bool color);
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return out_.is_open(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::uint64_t advisories_hidden() const noexcept { return seen_ - printed_; }

    void session_start(const Regime& r, double dv_budget_mps, double threshold, std::uint32_t abort_every,
                       const char* input);
    void input_rejected(const ConjunctionTelemetry& t, InputRejectReason why);
    void threat_scored(const ScoredThreat& s);
    void advisory(const LedgerRecord& r);
    void advisory_resolved(const LedgerRecord& r);
    void rollback(std::uint32_t alert_id, std::size_t unwound, double dv_budget_mps);
    void cycle_end(const CycleStats& c);
    void session_end(const CycleStats& totals);

private:
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    void log(const char* event, const char* fmt, ...) noexcept;
    void log_record(const char* event, const LedgerRecord& r) noexcept;

    std::string path_;
    std::ofstream out_;
    std::array<char, 1024> line_{};
    bool log_every_threat_;
    bool quiet_;
    Ansi a_;
    std::uint64_t seen_ = 0;
    std::uint64_t printed_ = 0;
};

}  // namespace ocas

#endif
