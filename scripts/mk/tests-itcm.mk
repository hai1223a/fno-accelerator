ITCM_TEST_TARGET ?= $(BUILD_DIR)/tests/component/itcm_ca_test

ITCM_TEST_SRC := \
	tests/component/itcm_ca_test.cpp \
	src/models/memory/itcm_ca.cpp \
	src/models/memory/sram_ca.cpp \
	src/sim/debug_logger.cpp

ITCM_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ITCM_TEST_SRC))
DEP += $(ITCM_TEST_OBJ:.o=.d)

.PHONY: test-itcm

$(ITCM_TEST_TARGET): $(ITCM_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-itcm: $(ITCM_TEST_TARGET)
	$(ITCM_TEST_TARGET)
