/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Native C++ CDM (Conjunction Data Message) Synthesizer
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Replaces legacy Python data/generate_cdms.py with native C++17.
 * Generates CCSDS-compliant orbital conjunction events for simulation.
 * ============================================================================
 */

#include "c_ingestion.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {

struct CDMScenario {
    uint32_t object_id;
    std::string object_name;
    double d_miss_m;
    double v_rel_kms;
    double t_tca_s;
    double cov_diag;
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    std::string output_path = "data/sample_cdms.json";
    if (argc > 1) {
        output_path = argv[1];
    }

    std::cout << "================================================================================\n"
              << "  OCAS: Native C++ CCSDS Conjunction Data Message (CDM) Generator\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "================================================================================\n\n";

    const std::vector<CDMScenario> scenarios = {
        {101, "STARLINK-4092 FRAGMENT", 24500.0, 1.82, 5400.0, 120.0},
        {102, "COSMOS-1408 ASAT DEBRIS",  1450.0, 4.25, 1800.0, 250.0},
        {103, "ISS-SHED RADIATOR PANEL",   110.0, 12.50,  420.0, 180.0},
        {104, "ORBCOMM SPARE (DEFUNCT)",  7800.0, 3.40, 2700.0, 380.0},
        {105, "CZ-3B ROCKET STAGE",        320.0, 9.80, 1100.0, 110.0},
        {106, "FENGYUN-1C DEBRIS (43122)", 680.0, 8.45,  780.0, 160.0},
        {107, "IRIDIUM-33 DEBRIS (34891)", 220.0, 11.20,  510.0,  95.0},
        {108, "SL-08 ROCKET BODY (22134)",9200.0, 2.75, 4100.0, 420.0},
        {109, "NOAA-16 DEBRIS (41103)",   1850.0, 5.10, 1950.0, 210.0},
        {110, "ENVISAT DERELICT",          450.0, 7.30,  890.0, 140.0}
    };

    const uint64_t base_epoch = 1726531200ULL; // Mission reference epoch

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "[ERROR] Could not open destination file: " << output_path << "\n";
        return 1;
    }

    out << "{\n"
        << "  \"metadata\": {\n"
        << "    \"standard\": \"CCSDS 508.0-B-1 CDM XML/JSON\",\n"
        << "    \"originator\": \"OCAS Autonomous Flight Software\",\n"
        << "    \"team_id\": \"DSCPP-III-2026-T018\",\n"
        << "    \"institution\": \"Graphic Era University\",\n"
        << "    \"generated_epoch_s\": " << base_epoch << ",\n"
        << "    \"count\": " << scenarios.size() << "\n"
        << "  },\n"
        << "  \"cdm_records\": [\n";

    for (size_t i = 0; i < scenarios.size(); ++i) {
        const auto& s = scenarios[i];
        out << "    {\n"
            << "      \"alert_id\": " << s.object_id << ",\n"
            << "      \"object_id\": " << (20000 + s.object_id) << ",\n"
            << "      \"object_name\": \"" << s.object_name << "\",\n"
            << "      \"miss_distance_m\": " << std::fixed << std::setprecision(2) << s.d_miss_m << ",\n"
            << "      \"rel_velocity_kms\": " << std::fixed << std::setprecision(3) << s.v_rel_kms << ",\n"
            << "      \"time_to_tca_s\": " << std::fixed << std::setprecision(1) << s.t_tca_s << ",\n"
            << "      \"covariance\": [\n"
            << "        [" << s.cov_diag << ", 0.0, 0.0],\n"
            << "        [0.0, " << s.cov_diag << ", 0.0],\n"
            << "        [0.0, 0.0, " << s.cov_diag << "]\n"
            << "      ],\n"
            << "      \"epoch_s\": " << (base_epoch + static_cast<uint64_t>(s.t_tca_s)) << "\n"
            << "    }" << (i + 1 < scenarios.size() ? "," : "") << "\n";

        std::cout << "  [SYNTHESIZED] CDM #" << s.object_id << ": "
                  << std::left << std::setw(28) << s.object_name << std::right
                  << " | d_miss=" << std::setw(7) << static_cast<int>(s.d_miss_m) << " m"
                  << " | v_rel=" << std::setw(5) << s.v_rel_kms << " km/s"
                  << " | TCA=" << std::setw(5) << static_cast<int>(s.t_tca_s) << " s\n";
    }

    out << "  ]\n}\n";
    out.close();

    std::cout << "\n[SUCCESS] Generated " << scenarios.size()
              << " CCSDS Conjunction Data Messages -> " << output_path << "\n\n";

    return 0;
}
