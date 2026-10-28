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

// An empty cov_trace_m2 field is legal and parses to NaN.
bool parse_cdm_csv_line(const char* line, ConjunctionTelemetry& out) noexcept;

// Round-trips exactly through parse_cdm_csv_line. Returns 0 if buf is too small.
std::size_t format_cdm_csv_line(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept;

struct CdmLoadReport {
    std::size_t lines = 0;
    std::size_t loaded = 0;
    std::size_t malformed = 0;
    std::size_t missing_covariance = 0;
    std::string error;
};

// max_rows == 0 means no limit.
bool load_cdm_csv(const std::string& path, std::vector<ConjunctionTelemetry>& out, CdmLoadReport& report,
                  std::size_t max_rows = 0);

bool write_cdm_csv(std::ostream& os, const std::vector<ConjunctionTelemetry>& rows);

}  // namespace ocas

#endif
