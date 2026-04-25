CXX ?= g++
BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/e203-sim

SYSTEMC_HOME ?=

# CPPFLAGS 存放头文件搜索路径, 宏定义, 预处理选项
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -g
LDFLAGS :=
LDLIBS := -lsystemc

ifneq ($(SYSTEMC_HOME),)
CPPFLAGS += -I$(SYSTEMC_HOME)/include
LDFLAGS += -L$(SYSTEMC_HOME)/lib -L$(SYSTEMC_HOME)/lib-linux64
endif

SRC := $(shell find src -name '*.cpp' 2>/dev/null)
OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

.PHONY: all build run test clean print-config

all: build

build: $(TARGET)

$(TARGET): $(OBJ)
	@if [ -z "$(SRC)" ]; then \
		echo "No source files found under src/."; \
		echo "Add src/sim/main.cpp before running make build."; \
		exit 1; \
	fi
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: build
	$(TARGET) -h configs/e203sim.json

print-config:
	@echo "CXX=$(CXX)"
	@echo "SYSTEMC_HOME=$(SYSTEMC_HOME)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"
	@echo "SRC=$(SRC)"
	@echo "TEST_SRC=$(TEST_SRC)"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEP)
