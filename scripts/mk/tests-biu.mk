BIU_TEST_TARGET ?= $(BUILD_DIR)/tests/component/biu_ca_test

BIU_TEST_SRC := \
	tests/component/biu_ca_test.cpp \
	src/models/bus/biu_ca.cpp \
	src/sim/thread_trace.cpp \
	src/sim/debug_logger.cpp

BIU_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(BIU_TEST_SRC))
DEP += $(BIU_TEST_OBJ:.o=.d)

.PHONY: test-biu

$(BIU_TEST_TARGET): $(BIU_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-biu: $(BIU_TEST_TARGET)
	$(BIU_TEST_TARGET)
