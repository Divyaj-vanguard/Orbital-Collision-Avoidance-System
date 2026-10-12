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
    src/ml/model_io.cpp
)
target_include_directories(ocas_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_compile_options(ocas_core PUBLIC ${OCAS_FLAGS})

# ---- Flight executive (Module 6) --------------------------------------------
# alloc_tracker.cpp replaces global operator new to verify zero allocations
# inside the cyclic loop, so it is linked into this executable only.
add_executable(ocas_flight_exec src/main.cpp src/core/alloc_tracker.cpp)
target_link_libraries(ocas_flight_exec PRIVATE ocas_core)

# ---- Verification suite -------------------------------------------------------
# ---- PyBind11 threat console (optional) --------------------------------------
# AutonomousOrbitalCollisionSystem/cpp/: Threat -> FIFO queue -> circular queue -> Python model (Random Forest or LSTM,
# via PyBind11) -> risk level -> priority queue. Needs a Python environment
# with torch, numpy, scikit-learn, pandas and pybind11 (ml/requirements.txt):
#   cmake -S . -B build -DOCAS_BUILD_PYBIND=ON -DPython_EXECUTABLE=.venv_lstm/bin/python
option(OCAS_BUILD_PYBIND "Build the PyBind11 threat console in AutonomousOrbitalCollisionSystem/cpp/" OFF)
if(OCAS_BUILD_PYBIND)
    set(PYBIND11_FINDPYTHON ON)
    find_package(Python REQUIRED COMPONENTS Interpreter Development.Embed)
    execute_process(COMMAND ${Python_EXECUTABLE} -m pybind11 --cmakedir
                    OUTPUT_VARIABLE pybind11_DIR OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${Python_EXECUTABLE} -c "import sysconfig; print(sysconfig.get_paths()['purelib'])"
                    OUTPUT_VARIABLE OCAS_PY_SITE OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    find_package(pybind11 CONFIG REQUIRED)
    set(OCAS_CONSOLE_DIR AutonomousOrbitalCollisionSystem/cpp)
    # One console per Python model: threat_console_rf and threat_console_lstm
    foreach(entry rf:AutonomousOrbitalCollisionSystem/ml lstm:ml)
        string(REPLACE ":" ";" parts ${entry})
        list(GET parts 0 model)
        list(GET parts 1 dir)
        add_executable(threat_console_${model} ${OCAS_CONSOLE_DIR}/main.cpp ${OCAS_CONSOLE_DIR}/CircularQueue.cpp)
        target_include_directories(threat_console_${model} PRIVATE ${OCAS_CONSOLE_DIR})
        target_link_libraries(threat_console_${model} PRIVATE pybind11::embed)
        target_compile_definitions(threat_console_${model} PRIVATE OCAS_ML_DIR="${CMAKE_SOURCE_DIR}/${dir}"
                                                                   OCAS_PY_SITE="${OCAS_PY_SITE}")
        set_target_properties(threat_console_${model} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    endforeach()
endif()

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
        convert_socrates:tools/datasets/convert_socrates.cpp
        convert_ccsds_cdm:tools/datasets/convert_ccsds_cdm.cpp
        train_lstm:tools/ml/train_lstm.cpp)
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
