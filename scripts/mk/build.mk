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
	$(TARGET) -c configs/e203sim.json

print-config:
	@echo "CXX=$(CXX)"
	@echo "SYSTEMC_HOME=$(SYSTEMC_HOME)"
	@echo "CPPFLAGS=$(CPPFLAGS)"
	@echo "CXXFLAGS=$(CXXFLAGS)"
	@echo "LDFLAGS=$(LDFLAGS)"
	@echo "LDLIBS=$(LDLIBS)"
	@echo "SRC=$(SRC)"
	@echo "DTCM_UNIT_TEST_SRC=$(DTCM_UNIT_TEST_SRC)"
	@echo "DTCM_TEST_SRC=$(DTCM_TEST_SRC)"

clean:
	rm -rf $(BUILD_DIR)
