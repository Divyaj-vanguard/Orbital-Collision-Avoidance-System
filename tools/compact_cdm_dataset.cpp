// ============================================================================
// OCAS ground tool: reduce the raw ESA Kelvins CDM CSV (103 columns) to the
// six-field OCAS schema. Reproduces data/cleaned_cdm_dataset.csv from
// test_data.csv.
//   alert_id            = event_id
//   miss_distance_m     = miss_distance                [m]
//   relative_speed_km_s = relative_speed / 1000        [m/s -> km/s]
//   time_to_tca_sec     = time_to_tca * 86400          [days -> s]
//   cov_trace_m2        = sum of the six squared sigmas (target + chaser RTN);
//                         empty if any sigma is missing
//   collision_prob      = 10^risk                      (ESA risk is log10 Pc)
// Usage: compact_cdm_dataset <esa_raw.csv> <out.csv>
// ============================================================================
#include "core/cdm_io.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::istringstream ss(line);
    while (std::getline(ss, cell, ',')) out.push_back(cell);
    if (!line.empty() && line.back() == ',') out.emplace_back();
    return out;
}

bool to_double(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end != s.c_str() && std::isfinite(v);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <esa_raw.csv> <out.csv>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 1;
    }
    std::string line;
    std::getline(in, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = split(line);
    auto col = [&](const char* name) {
        for (std::size_t i = 0; i < header.size(); ++i)
            if (header[i] == name) return static_cast<long>(i);
        return -1L;
    };
    const long c_id = col("event_id"), c_tca = col("time_to_tca"), c_risk = col("risk"),
               c_miss = col("miss_distance"), c_vel = col("relative_speed");
    const long c_sig[6] = {col("t_sigma_r"), col("t_sigma_t"), col("t_sigma_n"),
                           col("c_sigma_r"), col("c_sigma_t"), col("c_sigma_n")};
    for (long c : {c_id, c_tca, c_risk, c_miss, c_vel, c_sig[0], c_sig[1], c_sig[2], c_sig[3], c_sig[4], c_sig[5]}) {
        if (c < 0) {
            std::cerr << "input is not an ESA Kelvins CDM file (missing column)\n";
            return 1;
        }
    }
    std::vector<ocas::ConjunctionTelemetry> rows;
    std::size_t skipped = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto f = split(line);
        if (f.size() < header.size()) { ++skipped; continue; }
        ocas::ConjunctionTelemetry t{};
        double id = 0, tca = 0, risk = 0;
        if (!to_double(f[c_id], id) || !to_double(f[c_tca], tca) || !to_double(f[c_risk], risk) ||
            !to_double(f[c_miss], t.miss_distance_m) || !to_double(f[c_vel], t.relative_speed_km_s)) {
            ++skipped;
            continue;
        }
        t.alert_id = static_cast<std::uint32_t>(id);
        t.relative_speed_km_s /= 1000.0;
        t.time_to_tca_sec = tca * 86400.0;
        t.collision_prob = std::pow(10.0, risk);
        double trace = 0.0;
        bool ok = true;
        for (long c : c_sig) {
            double s = 0;
            ok = ok && to_double(f[c], s);
            trace += s * s;
        }
        t.cov_trace_m2 = ok ? trace : std::numeric_limits<double>::quiet_NaN();
        rows.push_back(t);
    }
    std::ofstream out(argv[2]);
    if (!ocas::write_cdm_csv(out, rows)) {
        std::cerr << "write failed: " << argv[2] << "\n";
        return 1;
    }
    std::cout << "wrote " << rows.size() << " rows to " << argv[2] << " (skipped " << skipped << ")\n";
    return 0;
}
