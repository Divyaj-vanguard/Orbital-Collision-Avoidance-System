# OCAS: Autonomous Orbital Collision Avoidance System. Plain C++17, no dependencies.
cmake_minimum_required(VERSION 3.16)
project(OCAS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_compile_options(-Wall -Wextra -Werror -pedantic -O3)

# The engine, shared by every program below.
add_library(ocas_core STATIC src/cdm_io.cpp src/model_io.cpp src/risk.cpp src/regime.cpp src/recorder.cpp
                             src/pipeline.cpp)
target_include_directories(ocas_core PUBLIC include)

# Flight executable. alloc_tracker.cpp counts every heap allocation, to prove the loop makes none.
add_executable(ocas_flight_exec src/main.cpp src/alloc_tracker.cpp)
target_link_libraries(ocas_flight_exec PRIVATE ocas_core)

add_executable(test_ocas tests/test_ocas.cpp)
target_link_libraries(test_ocas PRIVATE ocas_core)
target_compile_definitions(test_ocas PRIVATE OCAS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Offline tools: rebuild the input data and retrain the LSTM.
foreach(tool compact_cdm_dataset generate_synthetic_vectors train_lstm)
    add_executable(${tool} tools/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE ocas_core)
endforeach()
