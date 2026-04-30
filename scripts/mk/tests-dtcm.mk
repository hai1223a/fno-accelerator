DTCM_UNIT_TEST_TARGET ?= $(BUILD_DIR)/tests/unit/dtcm_sram_test
DTCM_TEST_TARGET ?= $(BUILD_DIR)/tests/component/dtcm_ca_test

DTCM_UNIT_TEST_SRC := \
	tests/unit/dtcm_sram_test.cpp \
	src/models/memory/sram_ca.cpp \
	src/sim/debug_logger.cpp

DTCM_TEST_SRC := \
	tests/component/dtcm_ca_test.cpp \
	src/models/memory/dtcm_ca.cpp \
	src/models/memory/sram_ca.cpp \
	src/sim/debug_logger.cpp

DTCM_UNIT_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(DTCM_UNIT_TEST_SRC))
DTCM_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(DTCM_TEST_SRC))
DEP += $(DTCM_UNIT_TEST_OBJ:.o=.d) $(DTCM_TEST_OBJ:.o=.d)

.PHONY: test-dtcm

$(DTCM_UNIT_TEST_TARGET): $(DTCM_UNIT_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

$(DTCM_TEST_TARGET): $(DTCM_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-dtcm: $(DTCM_UNIT_TEST_TARGET) $(DTCM_TEST_TARGET)
	$(DTCM_UNIT_TEST_TARGET)
	$(DTCM_TEST_TARGET)
