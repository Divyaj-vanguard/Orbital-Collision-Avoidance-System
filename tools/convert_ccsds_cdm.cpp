// ============================================================================
// OCAS ground tool: CCSDS 508.0-B-1 CDM (KVN text) -> OCAS six-field schema.
// Reads one or more KVN CDM files and writes one CSV row per file.
//   miss_distance_m     = MISS_DISTANCE [m]
//   relative_speed_km_s = RELATIVE_SPEED [m/s] / 1000
//   time_to_tca_sec     = TCA - CREATION_DATE
//   cov_trace_m2        = CR_R + CT_T + CN_N of OBJECT1 plus OBJECT2 (combined RTN trace)
//   collision_prob      = COLLISION_PROBABILITY if present, else 0
// Usage: convert_ccsds_cdm <out.csv> <cdm1.txt> [cdm2.txt ...]
// ============================================================================
#include "core/cdm_io.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    const auto e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

// CCSDS allows calendar (YYYY-MM-DDThh:mm:ss) and day-of-year (YYYY-DDDThh:mm:ss) forms.
bool parse_utc(const std::string& s, double& out) {
    int Y, M, D, h, m;
    double sec;
    std::tm tm{};
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%lf", &Y, &M, &D, &h, &m, &sec) == 6) {
        tm.tm_mon = M - 1;
        tm.tm_mday = D;
    } else if (std::sscanf(s.c_str(), "%d-%dT%d:%d:%lf", &Y, &D, &h, &m, &sec) == 5) {
        tm.tm_mon = 0;
        tm.tm_mday = D;  // timegm normalises day-of-year overflow into the right month
    } else {
        return false;
    }
    tm.tm_year = Y - 1900; tm.tm_hour = h; tm.tm_min = m;
    out = static_cast<double>(timegm(&tm)) + sec;
    return true;
}

bool convert(const char* path, std::uint32_t id, ocas::ConjunctionTelemetry& t) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    double creation = NAN, tca = NAN, trace = 0.0;
    int cov_terms = 0;
    t = ocas::ConjunctionTelemetry{};
    t.alert_id = id;
    t.miss_distance_m = NAN;
    t.relative_speed_km_s = NAN;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        const auto unit = val.find('[');
        if (unit != std::string::npos) val = trim(val.substr(0, unit));
        const double num = std::strtod(val.c_str(), nullptr);
        if (key == "CREATION_DATE") parse_utc(val, creation);
        else if (key == "TCA") parse_utc(val, tca);
        else if (key == "MISS_DISTANCE") t.miss_distance_m = num;
        else if (key == "RELATIVE_SPEED") t.relative_speed_km_s = num / 1000.0;
        else if (key == "COLLISION_PROBABILITY") t.collision_prob = num;
        else if (key == "CR_R" || key == "CT_T" || key == "CN_N") { trace += num; ++cov_terms; }
    }
    t.time_to_tca_sec = tca - creation;
    t.cov_trace_m2 = cov_terms == 6 ? trace : std::numeric_limits<double>::quiet_NaN();
    return std::isfinite(t.miss_distance_m) && std::isfinite(t.relative_speed_km_s) && std::isfinite(t.time_to_tca_sec);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " <out.csv> <cdm.txt> [...]\n";
        return 2;
    }
    std::vector<ocas::ConjunctionTelemetry> rows;
    for (int i = 2; i < argc; ++i) {
        ocas::ConjunctionTelemetry t{};
        if (convert(argv[i], static_cast<std::uint32_t>(i - 1), t)) rows.push_back(t);
        else std::cerr << "skipped (missing required keys): " << argv[i] << "\n";
    }
    std::ofstream out(argv[1]);
    if (!ocas::write_cdm_csv(out, rows)) return 1;
    std::cout << "wrote " << rows.size() << " rows to " << argv[1] << "\n";
    return 0;
}
