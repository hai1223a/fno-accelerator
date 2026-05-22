IFU_TEST_TARGET ?= $(BUILD_DIR)/tests/component/ifu_ca_test

IFU_TEST_SRC := \
	tests/component/ifu_ca_test.cpp \
	src/models/core/ifu_ca.cpp \
	src/models/memory/itcm_ca.cpp \
	src/models/memory/sram_ca.cpp \
	src/sim/pipe_trace.cpp \
	src/sim/thread_trace.cpp \
	src/sim/debug_logger.cpp

IFU_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(IFU_TEST_SRC))
DEP += $(IFU_TEST_OBJ:.o=.d)

.PHONY: test-ifu

$(IFU_TEST_TARGET): $(IFU_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-ifu: $(IFU_TEST_TARGET)
	$(IFU_TEST_TARGET)
