// ============================================================================
// OCAS ground tool: deterministic SYNTHETIC MEO and GEO test vectors.
// The ESA Kelvins data is LEO-only, so MEO/GEO behaviour is exercised with
// generated encounters. These are NOT real conjunctions and must not be used
// to report classifier accuracy.
//   seed 2026, own SplitMix64 PRNG (identical output on every platform)
//   collision_prob uses the isotropic approximation
//     Pc = R^2 / (2 s^2) * exp(-d^2 / (2 s^2)),  R = 20 m,  s^2 = Tr(C)/3
//   every 16th row has an empty covariance (exercises the fallback path)
// Usage: generate_synthetic_vectors <out_dir>
// ============================================================================
#include "core/cdm_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>

namespace {

struct SplitMix64 {
    std::uint64_t s;
    std::uint64_t next() {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(double lo, double hi) { return lo + (hi - lo) * (static_cast<double>(next() >> 11) * 0x1.0p-53); }
    double log_uniform(double lo, double hi) { return std::exp(uniform(std::log(lo), std::log(hi))); }
};

struct Profile {
    const char* file;
    std::uint32_t first_id;
    double d_lo, d_hi, v_lo, v_hi, t_lo, t_hi, s_lo, s_hi;
};

bool write(const std::string& dir, const Profile& p, SplitMix64& rng, int rows) {
    const std::string path = dir + "/" + p.file;
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return false;
    std::fprintf(f, "%s\n", ocas::kCdmCsvHeader);
    for (int i = 0; i < rows; ++i) {
        const double d = rng.log_uniform(p.d_lo, p.d_hi);
        const double v = rng.uniform(p.v_lo, p.v_hi);
        const double t = rng.uniform(p.t_lo, p.t_hi);
        const double s = rng.log_uniform(p.s_lo, p.s_hi);
        const double s2 = s * s;
        const double pc = std::min(1.0, 400.0 / (2.0 * s2) * std::exp(-d * d / (2.0 * s2)));
        if (i % 16 == 15) {
            std::fprintf(f, "%u,%.1f,%.3f,%.1f,,%.6g\n", p.first_id + i, d, v, t, pc);
        } else {
            std::fprintf(f, "%u,%.1f,%.3f,%.1f,%.2f,%.6g\n", p.first_id + i, d, v, t, 3.0 * s2, pc);
        }
    }
    std::fclose(f);
    std::cout << "wrote " << rows << " synthetic rows to " << path << "\n";
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "data";
    SplitMix64 rng{2026};
    //                 file                          id      d[m]           v[km/s]     t_TCA[s]              sigma[m]
    const Profile meo{"synthetic_meo_vectors.csv", 900000, 100.0, 50000.0, 0.1, 4.0, 2 * 3600.0, 5 * 86400.0, 200.0, 5000.0};
    const Profile geo{"synthetic_geo_vectors.csv", 950000, 200.0, 80000.0, 0.05, 1.5, 6 * 3600.0, 7 * 86400.0, 500.0, 10000.0};
    return write(dir, meo, rng, 48) && write(dir, geo, rng, 48) ? 0 : 1;
}
