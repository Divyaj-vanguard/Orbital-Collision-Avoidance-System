cmake_minimum_required(VERSION 3.16)
project(OCAS_Orbital_Collision_Avoidance
    VERSION 1.0.0
    DESCRIPTION "Autonomous Orbital Collision Avoidance System - Team DSCPP-III-2026-T018 (100% C/C++)"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Safety-Critical Strict Compiler Flags
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Werror -pedantic -O2 -g)
endif()

# Global Include Directories
include_directories(include)

# Module 1: C11 Ingestion Layer (Static Library)
add_library(ocas_c_ingestion STATIC
    src/c_ingestion/ring_buffer.c
    src/c_ingestion/fifo_queue.c
)
set_target_properties(ocas_c_ingestion PROPERTIES C_STANDARD 11)

# Modules 2-4: C++17 Core Engine (Risk, Propulsion, Persistence, Orchestrator)
add_library(ocas_cpp_engine STATIC
    src/cpp_engine/risk_engine.cpp
    src/cpp_engine/thruster_engine.cpp
    src/cpp_engine/persistence.cpp
    src/cpp_engine/ocas_system.cpp
)
target_link_libraries(ocas_cpp_engine PRIVATE ocas_c_ingestion)

# 1. Flight Executive Application Executable
add_executable(ocas_flight_exec src/main.cpp)
target_link_libraries(ocas_flight_exec PRIVATE ocas_cpp_engine ocas_c_ingestion)

# 2. Verification Test Suite Executable
add_executable(test_ocas src/tests/test_ocas.cpp)
target_link_libraries(test_ocas PRIVATE ocas_cpp_engine ocas_c_ingestion)

# 3. Native C++ CDM Synthesizer
add_executable(generate_cdms src/data_tools/generate_cdms.cpp)
target_link_libraries(generate_cdms PRIVATE ocas_cpp_engine ocas_c_ingestion)

# 4. Native C++ CelesTrak Ingestion & Tsiolkovsky Engine
add_executable(celestrak_ingest src/data_tools/celestrak_ingest.cpp)
target_link_libraries(celestrak_ingest PRIVATE ocas_cpp_engine ocas_c_ingestion)

# 5. Native C++ Embedded HTTP/1.1 Web Server
add_executable(ocas_web_server src/server/web_server.cpp)
target_link_libraries(ocas_web_server PRIVATE ocas_cpp_engine ocas_c_ingestion)

# Output all binaries directly to bin/
set_target_properties(ocas_flight_exec PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set_target_properties(test_ocas PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set_target_properties(generate_cdms PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set_target_properties(celestrak_ingest PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set_target_properties(ocas_web_server PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
