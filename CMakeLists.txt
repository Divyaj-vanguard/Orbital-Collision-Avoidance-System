# ============================================================================
# OCAS: Autonomous Orbital Collision Avoidance System
# Team DSCPP-III-2026-T018 | Graphic Era University
# Pure C++17 build. Every target compiles with
#   -std=c++17 -Wall -Wextra -Werror -pedantic -O3
# ============================================================================
cmake_minimum_required(VERSION 3.16)
project(OCAS VERSION 2.0.0
        DESCRIPTION "Autonomous Orbital Collision Avoidance System - evasion advisory engine"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(OCAS_BUILD_TOOLS "Build offline dataset and training tools" ON)

if(MSVC)
    set(OCAS_FLAGS /std:c++17 /W4 /WX /O2)
else()
    set(OCAS_FLAGS -std=c++17 -Wall -Wextra -Werror -pedantic -O3)
endif()

# ---- Core library: Modules 1-5 + orchestrator + telemetry -------------------
add_library(ocas_core STATIC
    src/core/types.cpp
    src/core/cdm_io.cpp
    src/risk/risk_classifier.cpp
    src/priority/max_heap.cpp
    src/regime/regime_factory.cpp
    src/persistence/state_caretaker.cpp
    src/persistence/mission_ledger.cpp
    src/persistence/flight_audit_logger.cpp
    src/pipeline/ocas_pipeline.cpp
    src/telemetry/terminal_telemetry.cpp
)
target_include_directories(ocas_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ocas_core PUBLIC ${OCAS_FLAGS})

# ---- Flight executive (Module 6) --------------------------------------------
# alloc_tracker.cpp replaces global operator new to verify zero allocations
# inside the cyclic loop, so it is linked into this executable only.
add_executable(ocas_flight_exec src/main.cpp src/core/alloc_tracker.cpp)
target_link_libraries(ocas_flight_exec PRIVATE ocas_core)

# ---- Verification suite -------------------------------------------------------
enable_testing()
add_executable(test_ocas tests/test_ocas.cpp)
target_link_libraries(test_ocas PRIVATE ocas_core)
add_test(NAME ocas_unit_tests COMMAND test_ocas)

# ---- Offline ground tools (not flight software) -----------------------------
if(OCAS_BUILD_TOOLS)
    foreach(tool clean_esa_dataset train_lr_model compact_cdm_dataset generate_synthetic_vectors)
        add_executable(${tool} tools/${tool}.cpp)
        target_link_libraries(${tool} PRIVATE ocas_core)
    endforeach()
endif()

set_target_properties(ocas_flight_exec test_ocas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(OCAS_BUILD_TOOLS)
    set_target_properties(clean_esa_dataset train_lr_model compact_cdm_dataset generate_synthetic_vectors
                          PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
