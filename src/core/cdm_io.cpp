// OCAS CDM CSV/JSON loader and serializer.
#include "core/cdm_io.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <ostream>

namespace ocas {

namespace {

// Parse a double field ending at ',' or end-of-line. Empty field -> NaN if allowed.
bool parse_double(const char*& p, double& out, bool allow_empty) noexcept {
    if (*p == ',' || *p == '\0' || *p == '\r' || *p == '\n') {
        if (!allow_empty) return false;
        out = std::numeric_limits<double>::quiet_NaN();
        return true;
    }
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(p, &end);
    if (end == p || errno == ERANGE || !std::isfinite(v)) return false;
    out = v;
    p = end;
    return true;
}

bool expect_comma(const char*& p) noexcept {
    if (*p != ',') return false;
    ++p;
    return true;
}

bool at_line_end(const char* p) noexcept { return *p == '\0' || *p == '\r' || *p == '\n'; }

std::size_t finish(int n, std::size_t cap) noexcept {
    return (n < 0 || static_cast<std::size_t>(n) >= cap) ? 0 : static_cast<std::size_t>(n);
}

}  // namespace

bool parse_cdm_csv_line(const char* line, ConjunctionTelemetry& out) noexcept {
    const char* p = line;
    if (*p < '0' || *p > '9') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long id = std::strtoul(p, &end, 10);
    if (end == p || errno == ERANGE || id > std::numeric_limits<std::uint32_t>::max()) return false;
    p = end;

    ConjunctionTelemetry t{};
    t.alert_id = static_cast<std::uint32_t>(id);
    if (!expect_comma(p) || !parse_double(p, t.miss_distance_m, false)) return false;
    if (!expect_comma(p) || !parse_double(p, t.relative_speed_km_s, false)) return false;
    if (!expect_comma(p) || !parse_double(p, t.time_to_tca_sec, false)) return false;
    if (!expect_comma(p) || !parse_double(p, t.cov_trace_m2, true)) return false;
    if (!expect_comma(p) || !parse_double(p, t.collision_prob, false)) return false;
    if (!at_line_end(p)) return false;
    if (t.collision_prob < 0.0 || t.collision_prob > 1.0) return false;
    out = t;
    return true;
}

std::size_t format_cdm_csv_line(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept {
    int n = 0;
    if (std::isfinite(t.cov_trace_m2)) {
        n = std::snprintf(buf, cap, "%u,%.17g,%.17g,%.17g,%.17g,%.17g", t.alert_id, t.miss_distance_m,
                          t.relative_speed_km_s, t.time_to_tca_sec, t.cov_trace_m2, t.collision_prob);
    } else {
        n = std::snprintf(buf, cap, "%u,%.17g,%.17g,%.17g,,%.17g", t.alert_id, t.miss_distance_m,
                          t.relative_speed_km_s, t.time_to_tca_sec, t.collision_prob);
    }
    return finish(n, cap);
}

std::size_t format_cdm_json(const ConjunctionTelemetry& t, char* buf, std::size_t cap) noexcept {
    char cov[32];
    if (std::isfinite(t.cov_trace_m2)) {
        std::snprintf(cov, sizeof cov, "%.17g", t.cov_trace_m2);
    } else {
        std::snprintf(cov, sizeof cov, "null");
    }
    const int n = std::snprintf(buf, cap,
                                "{\"alert_id\":%u,\"miss_distance_m\":%.17g,\"relative_speed_km_s\":%.17g,"
                                "\"time_to_tca_sec\":%.17g,\"cov_trace_m2\":%s,\"collision_prob\":%.17g}",
                                t.alert_id, t.miss_distance_m, t.relative_speed_km_s, t.time_to_tca_sec, cov,
                                t.collision_prob);
    return finish(n, cap);
}

bool load_cdm_csv(const std::string& path, std::vector<ConjunctionTelemetry>& out, CdmLoadReport& report,
                  std::size_t max_rows) {
    report = CdmLoadReport{};
    std::ifstream in(path);
    if (!in) {
        report.error = "cannot open " + path;
        return false;
    }
    std::string line;
    if (!std::getline(in, line)) {
        report.error = path + " is empty";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kCdmCsvHeader) {
        report.error = path + ": header mismatch, expected '" + std::string(kCdmCsvHeader) + "'";
        return false;
    }
    while (std::getline(in, line)) {
        ++report.lines;
        if (line.empty() || line == "\r") continue;
        ConjunctionTelemetry t{};
        if (!parse_cdm_csv_line(line.c_str(), t)) {
            if (report.malformed++ == 0) report.first_malformed_line = report.lines + 1;
            continue;
        }
        if (!std::isfinite(t.cov_trace_m2)) ++report.missing_covariance;
        out.push_back(t);
        ++report.loaded;
        if (max_rows != 0 && report.loaded >= max_rows) break;
    }
    return true;
}

bool write_cdm_csv(std::ostream& os, const std::vector<ConjunctionTelemetry>& rows) {
    os << kCdmCsvHeader << '\n';
    char buf[256];
    for (const auto& r : rows) {
        const std::size_t n = format_cdm_csv_line(r, buf, sizeof buf);
        if (n == 0) return false;
        os.write(buf, static_cast<std::streamsize>(n));
        os.put('\n');
    }
    return static_cast<bool>(os);
}

}  // namespace ocas
