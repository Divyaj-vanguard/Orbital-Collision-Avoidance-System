#include "cdm_io.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <ostream>

namespace ocas {

namespace {

bool at_field_end(const char* p) noexcept { return *p == ',' || *p == '\0' || *p == '\r' || *p == '\n'; }

// Reads ",<number>" (or "," followed by an empty field, giving NaN, when allowed).
bool next_double(const char*& p, double& out, bool allow_empty) noexcept {
    if (*p++ != ',') return false;
    if (at_field_end(p)) {
        out = std::numeric_limits<double>::quiet_NaN();
        return allow_empty;
    }
    char* end = nullptr;
    errno = 0;
    out = std::strtod(p, &end);
    if (end == p || errno == ERANGE || !std::isfinite(out)) return false;
    p = end;
    return true;
}

}  // namespace

bool parse_cdm_csv_line(const char* line, ConjunctionTelemetry& out) noexcept {
    if (*line < '0' || *line > '9') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long id = std::strtoul(line, &end, 10);
    if (errno == ERANGE || id > std::numeric_limits<std::uint32_t>::max()) return false;
    const char* p = end;
    ConjunctionTelemetry t{};
    t.alert_id = static_cast<std::uint32_t>(id);
    if (!next_double(p, t.miss_distance_m, false) || !next_double(p, t.relative_speed_km_s, false) ||
        !next_double(p, t.time_to_tca_sec, false) || !next_double(p, t.cov_trace_m2, true) ||
        !next_double(p, t.collision_prob, false) || *p == ',' || !at_field_end(p)) {
        return false;
    }
    if (t.collision_prob < 0.0 || t.collision_prob > 1.0) return false;
    out = t;
    return true;
}

std::size_t format_cdm_csv_line(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept {
    char cov[32] = "";
    if (std::isfinite(t.cov_trace_m2)) std::snprintf(cov, sizeof cov, "%.17g", t.cov_trace_m2);
    const int n = std::snprintf(buf, cap, "%u,%.17g,%.17g,%.17g,%s,%.17g", t.alert_id, t.miss_distance_m,
                                t.relative_speed_km_s, t.time_to_tca_sec, cov, t.collision_prob);
    return (n < 0 || static_cast<std::size_t>(n) >= cap) ? 0 : static_cast<std::size_t>(n);
}

bool load_cdm_csv(const std::string& path, std::vector<ConjunctionTelemetry>& out, CdmLoadReport& report,
                  std::size_t max_rows) {
    report = CdmLoadReport{};
    std::ifstream in(path);
    std::string line;
    if (!in) {
        report.error = "cannot open " + path;
        return false;
    }
    if (!std::getline(in, line)) {
        report.error = path + " is empty";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kCdmCsvHeader) {
        report.error = path + ": header mismatch, expected '" + std::string(kCdmCsvHeader) + "'";
        return false;
    }
    while (std::getline(in, line) && (max_rows == 0 || report.loaded < max_rows)) {
        if (line.empty() || line == "\r") continue;
        ConjunctionTelemetry t{};
        if (!parse_cdm_csv_line(line.c_str(), t)) {
            ++report.malformed;
            continue;
        }
        if (!std::isfinite(t.cov_trace_m2)) ++report.missing_covariance;
        out.push_back(t);
        ++report.loaded;
    }
    return true;
}

bool write_cdm_csv(std::ostream& os, const std::vector<ConjunctionTelemetry>& rows) {
    os << kCdmCsvHeader << '\n';
    char buf[256];
    for (const auto& r : rows) {
        const std::size_t n = format_cdm_csv_line(r, buf, sizeof buf);
        if (n == 0) return false;
        os.write(buf, static_cast<std::streamsize>(n)).put('\n');
    }
    return static_cast<bool>(os);
}

}  // namespace ocas
