// ============================================================================
// OCAS CDM data loader / serializer for the six-field compact schema:
//   alert_id,miss_distance_m,relative_speed_km_s,time_to_tca_sec,cov_trace_m2,collision_prob
// Loading happens before the cyclic loop and may allocate. Per-line parsing
// and serialization into a caller buffer do not allocate.
// An empty cov_trace_m2 field is legal (the ESA set has 6 such rows) and
// parses to NaN, which routes the CDM to the conservative fallback.
// ============================================================================
#ifndef OCAS_CORE_CDM_IO_HPP
#define OCAS_CORE_CDM_IO_HPP

#include "core/types.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace ocas {

inline constexpr const char* kCdmCsvHeader =
    "alert_id,miss_distance_m,relative_speed_km_s,time_to_tca_sec,cov_trace_m2,collision_prob";

/// Parse one data line (no header). Returns false on malformed input.
bool parse_cdm_csv_line(const char* line, ConjunctionTelemetry& out) noexcept;

/// Serialize one record as a CSV line without trailing newline. Round-trips
/// exactly through parse_cdm_csv_line (%.17g). Returns characters written,
/// or 0 if the buffer is too small.
std::size_t format_cdm_csv_line(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept;

/// Serialize one record as a compact JSON object (missing covariance -> null).
std::size_t format_cdm_json(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept;

struct CdmLoadReport {
    std::size_t lines = 0;
    std::size_t loaded = 0;
    std::size_t malformed = 0;
    std::size_t missing_covariance = 0;
    std::size_t first_malformed_line = 0;
    std::string error;  ///< Non-empty when the file could not be used at all
};

/// Load a whole CSV file. The header must match kCdmCsvHeader exactly.
/// max_rows == 0 means no limit.
bool load_cdm_csv(const std::string& path, std::vector<ConjunctionTelemetry>& out, CdmLoadReport& report,
                  std::size_t max_rows = 0);

/// Write header + rows. Returns false on stream failure.
bool write_cdm_csv(std::ostream& os, const std::vector<ConjunctionTelemetry>& rows);

}  // namespace ocas

#endif  // OCAS_CORE_CDM_IO_HPP
