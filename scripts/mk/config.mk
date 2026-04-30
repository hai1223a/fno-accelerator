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

DEP :=
