SRC := $(shell find src -name '*.cpp' 2>/dev/null)
OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC))
DEP += $(OBJ:.o=.d)

.PHONY: build run clean print-config

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
	$(TARGET) $(ARGS)

print-config:
	@echo "CXX=$(CXX)"
	@echo "SYSTEMC_HOME=$(SYSTEMC_HOME)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"
	@echo "AM_HOME=$(abspath $(AM_HOME))"
	@echo "AM_ARCH=$(AM_ARCH)"
	@echo "AM_CROSS_COMPILE=$(AM_CROSS_COMPILE)"
	@echo "AM_APP=$(AM_APP)"
	@echo "SRC=$(SRC)"
	@echo "DTCM_UNIT_TEST_SRC=$(DTCM_UNIT_TEST_SRC)"
	@echo "DTCM_TEST_SRC=$(DTCM_TEST_SRC)"

clean:
	rm -rf $(BUILD_DIR)
