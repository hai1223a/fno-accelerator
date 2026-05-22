CXX ?= g++
BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/e203-sim
CONFIG ?= configs/e203sim_pipetrace.json
BIN ?= build/tests/integration/bin/branch_ebreak.bin
ARGS ?= -c $(CONFIG) -i $(BIN)

SYSTEMC_HOME ?=

# CPPFLAGS 存放头文件搜索路径, 宏定义, 预处理选项
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -O0 -g
LDFLAGS :=
LDLIBS := -lsystemc -ldl

ifneq ($(SYSTEMC_HOME),)
CPPFLAGS += -I$(SYSTEMC_HOME)/include
LDFLAGS += -L$(SYSTEMC_HOME)/lib -L$(SYSTEMC_HOME)/lib-linux64
endif

DEP :=
