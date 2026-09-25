/**
 * ============================================================================
 * OCAS: Autonomous Orbital Collision Avoidance System
 * Native C++17 Embedded POSIX HTTP Server & Mission Control Gateway
 * Team ID: DSCPP-III-2026-T018 | Graphic Era University
 * ============================================================================
 * Completely eliminates Python server dependency.
 * Serves visualizer/ static assets and provides REST API endpoints:
 *   - GET  /api/telemetry
 *   - GET  /api/logs
 *   - POST /api/action
 * ============================================================================
 */

#include "c_ingestion.h"
#include "ocas_system.hpp"
#include "risk_engine.hpp"
#include "thruster_engine.hpp"
#include "persistence.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int DEFAULT_PORT = 8080;
constexpr size_t BUFFER_SIZE = 65536;

std::string get_mime_type(const std::string& path) {
    if (path.rfind(".html") != std::string::npos) return "text/html; charset=utf-8";
    if (path.rfind(".css") != std::string::npos) return "text/css; charset=utf-8";
    if (path.rfind(".js") != std::string::npos) return "application/javascript; charset=utf-8";
    if (path.rfind(".json") != std::string::npos) return "application/json; charset=utf-8";
    if (path.rfind(".png") != std::string::npos) return "image/png";
    if (path.rfind(".jpg") != std::string::npos || path.rfind(".jpeg") != std::string::npos) return "image/jpeg";
    if (path.rfind(".svg") != std::string::npos) return "image/svg+xml";
    if (path.rfind(".ico") != std::string::npos) return "image/x-icon";
    return "text/plain; charset=utf-8";
}

bool read_file_contents(const std::string& filepath, std::string& out_content) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;
    out_content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return true;
}

std::string build_http_response(int status_code, const std::string& status_text,
                                const std::string& content_type, const std::string& body) {
    std::ostringstream ss;
    ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
       << "Server: OCAS-Native-C++17\r\n"
       << "Access-Control-Allow-Origin: *\r\n"
       << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
       << "Access-Control-Allow-Headers: Content-Type\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;
    return ss.str();
}

std::string read_audit_logs_json(const std::string& log_filepath) {
    std::ifstream file(log_filepath);
    if (!file.is_open()) return "[]";

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.front() == '{' && line.back() == '}') {
            lines.push_back(line);
        }
    }

    size_t start_idx = lines.size() > 50 ? lines.size() - 50 : 0;
    std::ostringstream ss;
    ss << "[";
    for (size_t i = start_idx; i < lines.size(); ++i) {
        if (i > start_idx) ss << ",";
        ss << lines[i];
    }
    ss << "]";
    return ss.str();
}

void load_initial_cdms(ocas::OCASSystem& system) {
    const std::vector<CDMAlert> incoming_cdms = {
        {101, "STARLINK-4092 FRAGMENT", 24000.0, 1.8, 5400.0, {{120.0, 0, 0}, {0, 120.0, 0}, {0, 0, 120.0}}, 1726536600.0, 1, {0}},
        {102, "COSMOS-1408 ASAT DEBRIS",  1450.0, 4.2, 1800.0, {{250.0, 0, 0}, {0, 250.0, 0}, {0, 0, 250.0}}, 1726533000.0, 2, {0}},
        {103, "ISS-SHED RADIATOR PANEL",   110.0, 12.5,  420.0, {{180.0, 0, 0}, {0, 180.0, 0}, {0, 0, 180.0}}, 1726531620.0, 3, {0}},
        {104, "ORBCOMM SPARE (DEFUNCT)",  7800.0, 3.4, 2700.0, {{380.0, 0, 0}, {0, 380.0, 0}, {0, 0, 380.0}}, 1726533900.0, 4, {0}},
        {105, "CZ-3B ROCKET STAGE",        320.0, 9.8, 1100.0, {{110.0, 0, 0}, {0, 110.0, 0}, {0, 0, 110.0}}, 1726532300.0, 5, {0}}
    };

    for (const auto& cdm : incoming_cdms) {
        system.ingest_cdm(cdm);
    }
    system.process_and_prioritize_threats();
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) port = DEFAULT_PORT;
    }

    std::cout << "================================================================================\n"
              << "  OCAS: Native C++17 Embedded POSIX HTTP Server & Mission Control Gateway\n"
              << "  Team ID: DSCPP-III-2026-T018 | Graphic Era University\n"
              << "================================================================================\n\n";

    ocas::OCASSystem system("logs/flight_audit.log");

    SatelliteState nominal_state{};
    nominal_state.epoch_s = 1726531200.0;
    nominal_state.r[0] = 6786.0;
    nominal_state.r[1] = 0.0;
    nominal_state.r[2] = 120.0;
    nominal_state.v[0] = 0.0;
    nominal_state.v[1] = 7.66;
    nominal_state.v[2] = 0.05;
    nominal_state.dry_mass_kg = 1000.0;
    nominal_state.fuel_mass_kg = 200.0;
    nominal_state.status_flags = OCAS_STATUS_NOMINAL;
    nominal_state.sequence_id = 1;

    system.initialize(nominal_state);
    load_initial_cdms(system);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[ERROR] Could not create socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[WARN] setsockopt SO_REUSEADDR failed: " << std::strerror(errno) << "\n";
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::cerr << "[ERROR] Bind failed on port " << port << ": " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        std::cerr << "[ERROR] Listen failed: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    std::cout << "[SUCCESS] OCAS Native C++ Web Server active at http://localhost:" << port << "\n"
              << "          Serving visualizer/ assets & real-time telemetry endpoints.\n"
              << "          Press Ctrl+C to terminate.\n\n";

    double sim_time_s = nominal_state.epoch_s;
    uint32_t step_count = 1;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            continue;
        }

        std::vector<char> req_buf(BUFFER_SIZE, 0);
        ssize_t bytes_read = read(client_fd, req_buf.data(), req_buf.size() - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }

        std::string request(req_buf.data(), static_cast<size_t>(bytes_read));
        std::istringstream req_stream(request);
        std::string method, path, proto;
        req_stream >> method >> path >> proto;

        if (method == "OPTIONS") {
            std::string resp = build_http_response(200, "OK", "text/plain", "");
            (void)write(client_fd, resp.c_str(), resp.size());
            close(client_fd);
            continue;
        }

        std::string response_str;

        // --------------------------------------------------------------------
        // API Route: GET /api/telemetry
        // --------------------------------------------------------------------
        if (method == "GET" && path == "/api/telemetry") {
            std::string json_data = system.export_telemetry_json();
            response_str = build_http_response(200, "OK", "application/json", json_data);
        }
        // --------------------------------------------------------------------
        // API Route: GET /api/logs
        // --------------------------------------------------------------------
        else if (method == "GET" && path == "/api/logs") {
            std::string logs_json = read_audit_logs_json("logs/flight_audit.log");
            response_str = build_http_response(200, "OK", "application/json", logs_json);
        }
        // --------------------------------------------------------------------
        // API Route: POST /api/action
        // --------------------------------------------------------------------
        else if (method == "POST" && path == "/api/action") {
            // Find request body
            auto body_pos = request.find("\r\n\r\n");
            std::string body = (body_pos != std::string::npos) ? request.substr(body_pos + 4) : "";

            std::string action = "step";
            if (body.find("\"action\":\"inject_fault\"") != std::string::npos ||
                body.find("\"action\": \"inject_fault\"") != std::string::npos) {
                action = "inject_fault";
            } else if (body.find("\"action\":\"debris_surge\"") != std::string::npos ||
                       body.find("\"action\": \"debris_surge\"") != std::string::npos) {
                action = "debris_surge";
            } else if (body.find("\"action\":\"rollback\"") != std::string::npos ||
                       body.find("\"action\": \"rollback\"") != std::string::npos) {
                action = "rollback";
            } else if (body.find("\"action\":\"reset\"") != std::string::npos ||
                       body.find("\"action\": \"reset\"") != std::string::npos) {
                action = "reset";
            }

            bool burn_executed = false;
            std::string engine_used = "NONE";
            double duration_s = 0.0;

            if (action == "reset") {
                system.initialize(nominal_state);
                load_initial_cdms(system);
                sim_time_s = nominal_state.epoch_s;
                step_count = 1;
            } else if (action == "rollback") {
                system.trigger_abort_rollback();
            } else if (action == "debris_surge") {
                static uint32_t surge_id = 200;
                for (int i = 0; i < 3; ++i) {
                    CDMAlert alert{};
                    alert.alert_id = surge_id++;
                    alert.object_id = 90000 + alert.alert_id;
                    std::snprintf(alert.object_name, sizeof(alert.object_name), "SURGE_DEBRIS_%u", alert.alert_id);
                    alert.d_miss_m = 180.0 + (i * 350.0);
                    alert.v_rel_kms = 8.5 + (i * 1.5);
                    alert.t_tca_s = 600.0 + (i * 400.0);
                    alert.covariance[0][0] = 180.0;
                    alert.covariance[1][1] = 180.0;
                    alert.covariance[2][2] = 180.0;
                    alert.epoch_tca_s = sim_time_s + alert.t_tca_s;
                    system.ingest_cdm(alert);
                }
                system.process_and_prioritize_threats();
            } else if (action == "inject_fault") {
                // Execute next threat with primary anomaly to trigger fallback
                burn_executed = system.execute_top_threat_avoidance(true);
                engine_used = "CHEMICAL_BIPROP";
                duration_s = 8.0;
            } else { // "step"
                sim_time_s += 10.0;
                step_count++;

                SatelliteState cur = system.get_current_state();
                cur.epoch_s = sim_time_s;
                cur.sequence_id = step_count;
                // Propagate circular orbit
                double theta = (step_count * 0.05);
                cur.r[0] = 6786.0 * std::cos(theta);
                cur.r[1] = 6786.0 * std::sin(theta);
                system.ingest_telemetry(cur);

                if (!system.get_max_heap().is_empty()) {
                    burn_executed = system.execute_top_threat_avoidance(false);
                    engine_used = "CHEMICAL_BIPROP";
                    duration_s = 15.0;
                }
            }

            std::ostringstream resp_ss;
            resp_ss << "{"
                    << "\"status\":\"ok\","
                    << "\"action\":\"" << action << "\","
                    << "\"burn_executed\":" << (burn_executed ? "true" : "false") << ","
                    << "\"engine_used\":\"" << engine_used << "\","
                    << "\"burn_duration_s\":" << duration_s
                    << "}";
            response_str = build_http_response(200, "OK", "application/json", resp_ss.str());
        }
        // --------------------------------------------------------------------
        // Static File Route: visualizer/
        // --------------------------------------------------------------------
        else {
            if (path == "/" || path.empty()) {
                path = "/index.html";
            }

            // Sanitize path to prevent directory traversal
            if (path.find("..") != std::string::npos) {
                response_str = build_http_response(403, "Forbidden", "text/plain", "Access Denied");
            } else {
                std::string file_path = "visualizer" + path;
                std::string file_data;
                if (read_file_contents(file_path, file_data)) {
                    std::string mime = get_mime_type(file_path);
                    response_str = build_http_response(200, "OK", mime, file_data);
                } else {
                    response_str = build_http_response(404, "Not Found", "text/plain", "404 Not Found: " + path);
                }
            }
        }

        (void)write(client_fd, response_str.c_str(), response_str.size());
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
