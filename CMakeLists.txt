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

# ---- Core library -----------------------------------------------------------
add_library(ocas_core STATIC
    src/cdm_io.cpp
    src/model_io.cpp
    src/risk.cpp
    src/regime.cpp
    src/recorder.cpp
    src/pipeline.cpp
)
target_include_directories(ocas_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ocas_core PUBLIC ${OCAS_FLAGS})

# ---- Flight executive ---------------------------------------------------------
# alloc_tracker.cpp replaces global operator new to verify zero allocations
# inside the cyclic loop, so it is linked into this executable only.
add_executable(ocas_flight_exec src/main.cpp src/alloc_tracker.cpp)
target_link_libraries(ocas_flight_exec PRIVATE ocas_core)

# ---- Verification suite -------------------------------------------------------
enable_testing()
add_executable(test_ocas tests/test_ocas.cpp)
target_link_libraries(test_ocas PRIVATE ocas_core)
target_compile_definitions(test_ocas PRIVATE OCAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME ocas_unit_tests COMMAND test_ocas)

# ---- Offline ground tools (not flight software) -----------------------------
if(OCAS_BUILD_TOOLS)
    set(OCAS_TOOLS
        clean_esa_dataset:tools/clean_esa_dataset.cpp
        compact_cdm_dataset:tools/compact_cdm_dataset.cpp
        generate_synthetic_vectors:tools/generate_synthetic_vectors.cpp
        convert_socrates:tools/convert_socrates.cpp
        convert_ccsds_cdm:tools/convert_ccsds_cdm.cpp
        train_lstm:ml/lstm_cpp/train_lstm.cpp)
    foreach(entry ${OCAS_TOOLS})
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 tool)
        list(GET parts 1 src)
        add_executable(${tool} ${src})
        target_link_libraries(${tool} PRIVATE ocas_core)
        set_target_properties(${tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
endif()

set_target_properties(ocas_flight_exec test_ocas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
