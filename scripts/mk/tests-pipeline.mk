PIPELINE_STAGE_ORDER_TEST_TARGET ?= $(BUILD_DIR)/tests/component/pipeline_stage_order_test

PIPELINE_STAGE_ORDER_TEST_SRC := \
	tests/component/pipeline_stage_order_test.cpp

PIPELINE_STAGE_ORDER_TEST_OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(PIPELINE_STAGE_ORDER_TEST_SRC))
DEP += $(PIPELINE_STAGE_ORDER_TEST_OBJ:.o=.d)

.PHONY: test-pipeline-stage-order

$(PIPELINE_STAGE_ORDER_TEST_TARGET): $(PIPELINE_STAGE_ORDER_TEST_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

test-pipeline-stage-order: $(PIPELINE_STAGE_ORDER_TEST_TARGET)
	$(PIPELINE_STAGE_ORDER_TEST_TARGET)
