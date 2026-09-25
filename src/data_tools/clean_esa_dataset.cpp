/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * ESA Kelvins Collision Avoidance Challenge — Dataset Cleaner
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Reads train_data.csv and test_data.csv (103 columns, ESA CDM format).
 * Selects 20 physically meaningful features + derived labels.
 * Outputs:  data/train_clean.csv
 *           data/test_clean.csv
 *
 * COLUMN SELECTION RATIONALE (see comments per field below)
 * ============================================================================
 */

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal CSV parser — handles quoted fields
// ---------------------------------------------------------------------------
static std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    for (char c : line) {
        if (c == '"') { in_quotes = !in_quotes; }
        else if (c == ',' && !in_quotes) { fields.push_back(field); field.clear(); }
        else { field += c; }
    }
    fields.push_back(field);
    return fields;
}

static double safe_double(const std::string& s) {
    if (s.empty() || s == "nan" || s == "NaN" || s == "NULL" || s == "null")
        return 0.0;
    try { return std::stod(s); } catch (...) { return 0.0; }
}

// ---------------------------------------------------------------------------
// Object type encoder: categorical → numeric for model input
// DEBRIS=3 (most dangerous, no manoeuvre capability)
// ROCKET BODY=2 (large, trackable but uncontrolled)
// PAYLOAD=1 (may be controllable)
// UNKNOWN=0 / TBA=0
// ---------------------------------------------------------------------------
static double encode_object_type(const std::string& t) {
    if (t == "DEBRIS")       return 3.0;
    if (t == "ROCKET BODY")  return 2.0;
    if (t == "PAYLOAD")      return 1.0;
    return 0.0; // UNKNOWN / TBA
}

// ---------------------------------------------------------------------------
// Process one CSV file
// ---------------------------------------------------------------------------
static bool process_file(const std::string& input_path,
                         const std::string& output_path) {

    std::ifstream fin(input_path);
    if (!fin.is_open()) {
        std::cerr << "[ERROR] Cannot open: " << input_path << "\n";
        return false;
    }

    std::ofstream fout(output_path);
    if (!fout.is_open()) {
        std::cerr << "[ERROR] Cannot write: " << output_path << "\n";
        return false;
    }

    // -----------------------------------------------------------------------
    // Output header — 20 selected features + 2 derived labels
    // -----------------------------------------------------------------------
    fout << "event_id,"
         << "mission_id,"           // PRIMARY: ESA satellite ID (anonymised)
         << "c_object_type_enc,"    // SECONDARY: debris/payload/rocket-body encoded
         // --- Core conjunction geometry ---
         << "time_to_tca_sec,"      // time to closest approach (converted days→sec)
         << "miss_distance_m,"      // scalar miss distance
         << "relative_speed_ms,"    // scalar closing speed
         // --- RTN relative position: needed for Mahalanobis & burn geometry ---
         << "rel_pos_r_m,"          // radial component of miss vector
         << "rel_pos_t_m,"          // transverse (along-track)
         << "rel_pos_n_m,"          // normal (cross-track)
         // --- RTN relative velocity ---
         << "rel_vel_r_ms,"
         << "rel_vel_t_ms,"
         << "rel_vel_n_ms,"
         // --- Covariance (position uncertainty, 1-sigma, m) ---
         // Why: 200m miss + 50m sigma = lethal. 200m miss + 5000m sigma = probably fine.
         << "t_sigma_r_m,"          // primary radial 1-sigma
         << "t_sigma_t_m,"          // primary transverse 1-sigma
         << "t_sigma_n_m,"          // primary normal 1-sigma
         << "c_sigma_r_m,"          // secondary radial 1-sigma
         << "c_sigma_t_m,"
         << "c_sigma_n_m,"
         // --- Mahalanobis distance: miss normalised by combined uncertainty ---
         // Why: the single best scalar risk proxy — used by NASA CARA directly
         << "mahalanobis_distance,"
         // --- Orbital regime (altitude) ---
         // Why: debris density varies drastically by altitude (LEO vs GEO)
         << "t_h_per_km,"           // primary perigee altitude
         << "c_h_per_km,"           // secondary perigee altitude
         // --- Space weather: solar flux drives atmospheric drag → affects orbit pred ---
         // Why: high F10.7 → denser atmosphere → larger position uncertainty at TCA
         << "F10,"
         << "AP,"                   // geomagnetic index
         // --- LABELS (derived) ---
         << "p_collision,"          // actual P_col = 10^risk  (float, for regression)
         << "is_actionable\n";      // binary: 1 if P_col > 1e-4 (NASA manoeuvre threshold)

    // -----------------------------------------------------------------------
    // Parse header of input file
    // -----------------------------------------------------------------------
    std::string header_line;
    std::getline(fin, header_line);
    // remove trailing \r if present
    if (!header_line.empty() && header_line.back() == '\r')
        header_line.pop_back();

    auto header = parse_csv_line(header_line);
    std::map<std::string, int> col_idx;
    for (int i = 0; i < (int)header.size(); ++i)
        col_idx[header[i]] = i;

    auto get = [&](const std::vector<std::string>& fields, const std::string& name) -> std::string {
        auto it = col_idx.find(name);
        if (it == col_idx.end()) return "";
        int idx = it->second;
        return (idx < (int)fields.size()) ? fields[idx] : "";
    };

    // -----------------------------------------------------------------------
    // Process rows
    // -----------------------------------------------------------------------
    std::string line;
    long long row_count = 0, written = 0, skipped_nulls = 0;

    while (std::getline(fin, line)) {
        ++row_count;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        auto f = parse_csv_line(line);

        // --- Extract all needed fields ---
        std::string event_id    = get(f, "event_id");
        std::string mission_id  = get(f, "mission_id");
        std::string c_obj_type  = get(f, "c_object_type");

        double time_to_tca_days = safe_double(get(f, "time_to_tca"));
        double miss_distance    = safe_double(get(f, "miss_distance"));
        double relative_speed   = safe_double(get(f, "relative_speed"));
        double rel_pos_r        = safe_double(get(f, "relative_position_r"));
        double rel_pos_t        = safe_double(get(f, "relative_position_t"));
        double rel_pos_n        = safe_double(get(f, "relative_position_n"));
        double rel_vel_r        = safe_double(get(f, "relative_velocity_r"));
        double rel_vel_t        = safe_double(get(f, "relative_velocity_t"));
        double rel_vel_n        = safe_double(get(f, "relative_velocity_n"));
        double t_sigma_r        = safe_double(get(f, "t_sigma_r"));
        double t_sigma_t        = safe_double(get(f, "t_sigma_t"));
        double t_sigma_n        = safe_double(get(f, "t_sigma_n"));
        double c_sigma_r        = safe_double(get(f, "c_sigma_r"));
        double c_sigma_t        = safe_double(get(f, "c_sigma_t"));
        double c_sigma_n        = safe_double(get(f, "c_sigma_n"));
        double mahal_dist       = safe_double(get(f, "mahalanobis_distance"));
        double t_h_per          = safe_double(get(f, "t_h_per"));
        double c_h_per          = safe_double(get(f, "c_h_per"));
        double f10              = safe_double(get(f, "F10"));
        double ap               = safe_double(get(f, "AP"));
        double risk_log10       = safe_double(get(f, "risk"));

        // --- Skip rows where core physics fields are zero/missing ---
        if (miss_distance == 0.0 && relative_speed == 0.0) {
            ++skipped_nulls;
            continue;
        }

        // --- Derive labels ---
        // risk is log10(P_col) — convert to actual probability
        double p_collision  = std::pow(10.0, risk_log10);
        int is_actionable   = (p_collision > 1e-4) ? 1 : 0;

        // --- Unit conversions ---
        double time_to_tca_sec = time_to_tca_days * 86400.0;
        double obj_type_enc    = encode_object_type(c_obj_type);

        // --- Write cleaned row ---
        fout << std::fixed;
        fout << event_id          << ","
             << mission_id        << ","
             << obj_type_enc      << ","
             << time_to_tca_sec   << ","
             << miss_distance     << ","
             << relative_speed    << ","
             << rel_pos_r         << ","
             << rel_pos_t         << ","
             << rel_pos_n         << ","
             << rel_vel_r         << ","
             << rel_vel_t         << ","
             << rel_vel_n         << ","
             << t_sigma_r         << ","
             << t_sigma_t         << ","
             << t_sigma_n         << ","
             << c_sigma_r         << ","
             << c_sigma_t         << ","
             << c_sigma_n         << ","
             << mahal_dist        << ","
             << t_h_per           << ","
             << c_h_per           << ","
             << f10               << ","
             << ap                << ","
             << p_collision       << ","
             << is_actionable     << "\n";
        ++written;
    }

    fin.close();
    fout.close();

    std::cout << "  Input rows   : " << row_count    << "\n";
    std::cout << "  Written rows : " << written      << "\n";
    std::cout << "  Skipped(null): " << skipped_nulls << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::cout << "=============================================================\n"
              << "  OCAS ESA Dataset Cleaner — Feature Selection & Labelling\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "=============================================================\n\n";

    const std::string base = (argc > 1) ? std::string(argv[1]) : ".";

    struct FileSpec { std::string in, out; };
    std::vector<FileSpec> files = {
        { base + "/train_data.csv", base + "/data/train_clean.csv" },
        { base + "/test_data.csv",  base + "/data/test_clean.csv"  }
    };

    for (auto& spec : files) {
        std::cout << "[CLEANING] " << spec.in << "\n";
        std::cout << "        -> " << spec.out << "\n";
        if (!process_file(spec.in, spec.out)) {
            std::cerr << "[FAILED]\n";
            return 1;
        }
        std::cout << "  [OK]\n\n";
    }

    std::cout << "=============================================================\n"
              << "  SELECTED FEATURES (25 total from 103):\n"
              << "=============================================================\n"
              << "  IDENTITY      : event_id, mission_id (primary), c_object_type_enc (secondary)\n"
              << "  CORE GEOMETRY : time_to_tca_sec, miss_distance_m, relative_speed_ms\n"
              << "  RTN POSITION  : rel_pos_r/t/n  (miss vector decomposed)\n"
              << "  RTN VELOCITY  : rel_vel_r/t/n  (closing velocity decomposed)\n"
              << "  COVARIANCE    : t/c_sigma_r/t/n (6 uncertainty sigmas)\n"
              << "  DERIVED       : mahalanobis_distance (miss / combined sigma)\n"
              << "  ORBITAL REGIME: t_h_per_km, c_h_per_km (altitude → debris density)\n"
              << "  SPACE WEATHER : F10 (solar flux), AP (geomagnetic index)\n"
              << "  LABELS        : p_collision (float), is_actionable (0/1)\n"
              << "=============================================================\n\n"
              << "  NOTE: Primary satellite NAMES are anonymised by ESA (mission_id is\n"
              << "        a numeric code). Secondary object type is preserved as:\n"
              << "        DEBRIS=3, ROCKET BODY=2, PAYLOAD=1, UNKNOWN=0\n"
              << "=============================================================\n";
    return 0;
}
