EXU_TEST_TARGET ?= $(BUILD_DIR)/tests/component/exu_ca_test

# EXU component test 只链接 EXU 所需最小依赖，避免启动完整 SoC。
EXU_TEST_SRC := \
	tests/component/exu_ca_test.cpp \
	src/models/core/exu_ca.cpp \
	src/models/core/lsu_ca.cpp \
	src/models/core/regfile.cpp \
	src/models/core/alu_unit.cpp \
	src/isa/rv32i_decode.cpp \
	src/models/memory/dtcm_ca.cpp \
	src/models/memory/sram_ca.cpp \
	src/sim/pipe_trace.cpp \
	src/sim/thread_trace.cpp \
	src/sim/difftest_manager.cpp \
	src/sim/debug_logger.cpp

EXU_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(EXU_TEST_SRC))
DEP += $(EXU_TEST_OBJ:.o=.d)

.PHONY: test-exu

$(EXU_TEST_TARGET): $(EXU_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-exu: $(EXU_TEST_TARGET)
	$(EXU_TEST_TARGET)
