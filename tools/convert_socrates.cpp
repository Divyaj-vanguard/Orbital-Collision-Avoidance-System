// ============================================================================
// OCAS ground tool: CelesTrak SOCRATES + SATCAT -> OCAS six-field schema,
// split by the primary object's orbital regime (LEO / MEO / GEO).
//
// Field mapping (SOCRATES CSV, documented at celestrak.org/SOCRATES/socrates-format.php):
//   alert_id            = row index (SOCRATES has no event id)
//   miss_distance_m     = TCA_RANGE [km] * 1000
//   relative_speed_km_s = TCA_RELATIVE_SPEED [km/s]
//   time_to_tca_sec     = TCA - reference epoch (the download time, passed in)
//   cov_trace_m2        = EMPTY. SOCRATES screens TLEs and publishes no covariance,
//                         so every row routes to the conservative fallback.
//   collision_prob      = MAX_PROB. This is the MAXIMUM Pc over all covariance
//                         sizes (an upper bound), not an operational Pc.
// Regime: mean altitude (APOGEE + PERIGEE)/2 of NORAD_CAT_ID_1 from SATCAT,
// classified with RegimeFactory::classify so the boundaries match the flight code.
//
// Usage: convert_socrates <socrates.csv> <satcat.csv> <reference_utc ISO8601> <out_dir>
// ============================================================================
#include "core/cdm_io.hpp"
#include "regime/regime_strategy.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// RFC 4180 split: handles quoted fields with embedded commas.
std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
            else if (c == '"') quoted = false;
            else cur += c;
        } else if (c == '"') quoted = true;
        else if (c == ',') { out.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    out.push_back(cur);
    return out;
}

long find_col(const std::vector<std::string>& h, const char* name) {
    for (std::size_t i = 0; i < h.size(); ++i) if (h[i] == name) return static_cast<long>(i);
    return -1;
}

bool to_d(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* e = nullptr;
    v = std::strtod(s.c_str(), &e);
    return e != s.c_str() && std::isfinite(v);
}

// "YYYY-MM-DD HH:MM:SS.sss" or "YYYY-MM-DDTHH:MM:SS(.sss)Z" -> seconds since epoch (UTC).
bool parse_utc(const std::string& s, double& out) {
    int Y, M, D, h, m;
    double sec;
    if (std::sscanf(s.c_str(), "%d-%d-%d%*c%d:%d:%lf", &Y, &M, &D, &h, &m, &sec) != 6) return false;
    std::tm tm{};
    tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D; tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = 0;
    out = static_cast<double>(timegm(&tm)) + sec;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: " << argv[0] << " <socrates.csv> <satcat.csv> <reference_utc> <out_dir>\n";
        return 2;
    }
    double ref = 0.0;
    if (!parse_utc(argv[3], ref)) { std::cerr << "bad reference time " << argv[3] << "\n"; return 2; }

    // ---- SATCAT: NORAD id -> mean altitude [km]
    std::unordered_map<long, double> altitude;
    {
        std::ifstream in(argv[2]);
        std::string line;
        if (!in || !std::getline(in, line)) { std::cerr << "cannot read " << argv[2] << "\n"; return 1; }
        const auto h = split_csv(line);
        const long c_id = find_col(h, "NORAD_CAT_ID"), c_ap = find_col(h, "APOGEE"), c_pe = find_col(h, "PERIGEE");
        if (c_id < 0 || c_ap < 0 || c_pe < 0) { std::cerr << "SATCAT header not recognised\n"; return 1; }
        while (std::getline(in, line)) {
            const auto f = split_csv(line);
            double id, ap, pe;
            if (f.size() < h.size() || !to_d(f[c_id], id) || !to_d(f[c_ap], ap) || !to_d(f[c_pe], pe)) continue;
            altitude[static_cast<long>(id)] = 0.5 * (ap + pe);
        }
    }

    // ---- SOCRATES rows
    std::ifstream in(argv[1]);
    std::string line;
    if (!in || !std::getline(in, line)) { std::cerr << "cannot read " << argv[1] << "\n"; return 1; }
    const auto h = split_csv(line);
    const long c_id1 = find_col(h, "NORAD_CAT_ID_1"), c_tca = find_col(h, "TCA"), c_rng = find_col(h, "TCA_RANGE"),
               c_vel = find_col(h, "TCA_RELATIVE_SPEED"), c_pc = find_col(h, "MAX_PROB");
    if (c_id1 < 0 || c_tca < 0 || c_rng < 0 || c_vel < 0 || c_pc < 0) { std::cerr << "SOCRATES header not recognised\n"; return 1; }

    std::vector<ocas::ConjunctionTelemetry> by_regime[3];
    std::size_t no_satcat = 0, bad = 0, past = 0, invalid_alt = 0;
    std::uint32_t row = 0;
    while (std::getline(in, line)) {
        ++row;
        const auto f = split_csv(line);
        double id1, rng, vel, pc, tca;
        if (f.size() < h.size() || !to_d(f[c_id1], id1) || !to_d(f[c_rng], rng) || !to_d(f[c_vel], vel) ||
            !to_d(f[c_pc], pc) || !parse_utc(f[c_tca], tca)) { ++bad; continue; }
        const auto it = altitude.find(static_cast<long>(id1));
        if (it == altitude.end()) { ++no_satcat; continue; }
        ocas::OrbitalRegime r{};
        if (!ocas::RegimeFactory::classify(it->second, r)) { ++invalid_alt; continue; }
        ocas::ConjunctionTelemetry t{};
        t.alert_id = row;
        t.miss_distance_m = rng * 1000.0;
        t.relative_speed_km_s = vel;
        t.time_to_tca_sec = tca - ref;
        t.cov_trace_m2 = std::numeric_limits<double>::quiet_NaN();
        t.collision_prob = pc < 0.0 ? 0.0 : (pc > 1.0 ? 1.0 : pc);
        if (t.time_to_tca_sec <= 0.0) ++past;  // kept; the pipeline rejects them as PAST_TCA
        by_regime[static_cast<int>(r)].push_back(t);
    }

    const char* names[3] = {"socrates_leo.csv", "socrates_meo.csv", "socrates_geo.csv"};
    for (int i = 0; i < 3; ++i) {
        const std::string path = std::string(argv[4]) + "/" + names[i];
        std::ofstream out(path);
        if (!ocas::write_cdm_csv(out, by_regime[i])) { std::cerr << "write failed " << path << "\n"; return 1; }
        std::cout << "wrote " << by_regime[i].size() << " rows to " << path << "\n";
    }
    std::cout << "rows read " << row << " | malformed " << bad << " | primary not in SATCAT " << no_satcat
              << " | sub-orbital altitude " << invalid_alt << " | TCA already past at reference " << past << "\n";
    return 0;
}
