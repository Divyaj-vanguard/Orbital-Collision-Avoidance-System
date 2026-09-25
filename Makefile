# ============================================================================
# OCAS: Autonomous Orbital Collision Avoidance System
# Safety-Critical Flight Software Build System (100% C/C++, Zero Python)
# Team ID: DSCPP-III-2026-T018 | Graphic Era University
# ============================================================================

CC       ?= gcc
CXX      ?= g++
AR       ?= ar

CFLAGS   := -Wall -Wextra -Werror -pedantic -std=c11 -O2 -g -Iinclude
CXXFLAGS := -Wall -Wextra -Werror -pedantic -std=c++17 -O2 -g -Iinclude
LDFLAGS  := -lm

BUILD_DIR := build
BIN_DIR   := bin
LIB_DIR   := lib
LOGS_DIR  := logs

C_SRCS   := src/c_ingestion/ring_buffer.c src/c_ingestion/fifo_queue.c
CPP_SRCS := src/cpp_engine/risk_engine.cpp \
            src/cpp_engine/thruster_engine.cpp \
            src/cpp_engine/persistence.cpp \
            src/cpp_engine/ocas_system.cpp

C_OBJS   := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(C_SRCS))
CPP_OBJS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CPP_SRCS))

STATIC_LIB_C   := $(LIB_DIR)/libocas_c.a
STATIC_LIB_CPP := $(LIB_DIR)/libocas_cpp.a

MAIN_BIN       := $(BIN_DIR)/ocas_flight_exec
TEST_BIN       := $(BIN_DIR)/test_ocas
GEN_CDMS_BIN   := $(BIN_DIR)/generate_cdms
CELESTRAK_BIN  := $(BIN_DIR)/celestrak_ingest
SERVER_BIN     := $(BIN_DIR)/ocas_web_server

.PHONY: all clean test run dirs server celestrak cdms

all: dirs $(MAIN_BIN) $(TEST_BIN) $(GEN_CDMS_BIN) $(CELESTRAK_BIN) $(SERVER_BIN)

dirs:
	@mkdir -p $(BUILD_DIR)/c_ingestion $(BUILD_DIR)/cpp_engine $(BUILD_DIR)/data_tools $(BUILD_DIR)/server $(BUILD_DIR)/tests $(BIN_DIR) $(LIB_DIR) $(LOGS_DIR) data

# C11 Object Compilation
$(BUILD_DIR)/c_ingestion/%.o: src/c_ingestion/%.c | dirs
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# C++17 Object Compilation
$(BUILD_DIR)/cpp_engine/%.o: src/cpp_engine/%.cpp | dirs
	@echo "[CXX] $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# Static Archives
$(STATIC_LIB_C): $(C_OBJS) | dirs
	@echo "[AR]  $@"
	@$(AR) rcs $@ $^

$(STATIC_LIB_CPP): $(CPP_OBJS) | dirs
	@echo "[AR]  $@"
	@$(AR) rcs $@ $^

# Main Flight Executive Binary
$(MAIN_BIN): src/main.cpp $(STATIC_LIB_CPP) $(STATIC_LIB_C) | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -locas_cpp -locas_c $(LDFLAGS) -o $@

# Verification Test Suite Binary
$(TEST_BIN): src/tests/test_ocas.cpp $(STATIC_LIB_CPP) $(STATIC_LIB_C) | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -locas_cpp -locas_c $(LDFLAGS) -o $@

# Native C++ CCSDS CDM Synthesizer
$(GEN_CDMS_BIN): src/data_tools/generate_cdms.cpp $(STATIC_LIB_CPP) $(STATIC_LIB_C) | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -locas_cpp -locas_c $(LDFLAGS) -o $@

# Native C++ CelesTrak Ingestion, Logistic Regression & Tsiolkovsky Engine
$(CELESTRAK_BIN): src/data_tools/celestrak_ingest.cpp $(STATIC_LIB_CPP) $(STATIC_LIB_C) | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -locas_cpp -locas_c $(LDFLAGS) -o $@

# Native C++ Embedded HTTP/1.1 Server
$(SERVER_BIN): src/server/web_server.cpp $(STATIC_LIB_CPP) $(STATIC_LIB_C) | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< -L$(LIB_DIR) -locas_cpp -locas_c $(LDFLAGS) -o $@

test: $(TEST_BIN)
	@echo ""
	@echo ">>> Running automated flight software verification test suite..."
	@./$(TEST_BIN)

run: $(MAIN_BIN)
	@echo ""
	@echo ">>> Launching OCAS Flight Executive Simulation..."
	@./$(MAIN_BIN)

cdms: $(GEN_CDMS_BIN)
	@./$(GEN_CDMS_BIN)

celestrak: $(CELESTRAK_BIN)
	@./$(CELESTRAK_BIN)

server: $(SERVER_BIN)
	@./$(SERVER_BIN) 8080

# ESA Kelvins Dataset Cleaner — 103 cols → 25 selected features
CLEAN_ESA_BIN := $(BIN_DIR)/clean_esa_dataset
$(CLEAN_ESA_BIN): src/data_tools/clean_esa_dataset.cpp | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

clean_data: $(CLEAN_ESA_BIN)
	@echo ">>> Cleaning ESA Kelvins dataset..."
	@./$(CLEAN_ESA_BIN) .

# Logistic Regression Weight Trainer — reads cleaned ESA dataset, trains 5 weights
TRAIN_LR_BIN := $(BIN_DIR)/train_lr_model
$(TRAIN_LR_BIN): src/data_tools/train_lr_model.cpp | dirs
	@echo "[LD]  $@"
	@$(CXX) $(CXXFLAGS) $< $(LDFLAGS) -o $@

train_model: $(TRAIN_LR_BIN)
	@echo ">>> Training Logistic Regression on ESA CDM dataset..."
	@./$(TRAIN_LR_BIN) .

clean:
	@rm -rf $(BUILD_DIR) $(LIB_DIR) $(BIN_DIR)
	@echo "Clean complete."
