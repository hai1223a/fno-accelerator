PERIPHERAL_TEST_TARGET ?= $(BUILD_DIR)/tests/component/peripheral_at_test

PERIPHERAL_TEST_SRC := \
	tests/component/peripheral_at_test.cpp \
	src/models/peripherals/clint_at.cpp \
	src/models/peripherals/uart0_at.cpp \
	src/sim/debug_logger.cpp

PERIPHERAL_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PERIPHERAL_TEST_SRC))
DEP += $(PERIPHERAL_TEST_OBJ:.o=.d)

.PHONY: test-peripheral

$(PERIPHERAL_TEST_TARGET): $(PERIPHERAL_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-peripheral: $(PERIPHERAL_TEST_TARGET)
	$(PERIPHERAL_TEST_TARGET)
