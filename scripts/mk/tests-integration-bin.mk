RISCV_CROSS_COMPILE ?= riscv64-unknown-linux-gnu-
RISCV_CC ?= $(RISCV_CROSS_COMPILE)gcc
RISCV_OBJCOPY ?= $(RISCV_CROSS_COMPILE)objcopy
RISCV_OBJDUMP ?= $(RISCV_CROSS_COMPILE)objdump

INTEGRATION_BIN_DIR ?= $(BUILD_DIR)/tests/integration/bin
INTEGRATION_LDSCRIPT := tests/integration/linker.ld
INTEGRATION_TEST_NAMES := add_ebreak load_store_ebreak branch_ebreak m_extension_ebreak zicsr_ebreak fencei_ebreak
INTEGRATION_ELFS := $(addprefix $(INTEGRATION_BIN_DIR)/,$(addsuffix .elf,$(INTEGRATION_TEST_NAMES)))
INTEGRATION_BINS := $(INTEGRATION_ELFS:.elf=.bin)
INTEGRATION_DUMPS := $(INTEGRATION_ELFS:.elf=.txt)

RISCV_BARE_CFLAGS := -march=rv32im_zicsr_zifencei -mabi=ilp32 -ffreestanding -fno-pic -fno-builtin -nostdlib -nostartfiles
RISCV_BARE_LDFLAGS := -Wl,-T,$(INTEGRATION_LDSCRIPT) -Wl,--no-relax -Wl,--gc-sections

.PHONY: test-bin test-integration-bin clean-test-bin

test-bin: test-integration-bin

test-integration-bin: $(INTEGRATION_ELFS) $(INTEGRATION_BINS) $(INTEGRATION_DUMPS)

$(INTEGRATION_BIN_DIR)/%.elf: tests/integration/%.S $(INTEGRATION_LDSCRIPT)
	@mkdir -p $(dir $@)
	$(RISCV_CC) $(RISCV_BARE_CFLAGS) $(RISCV_BARE_LDFLAGS) $< -o $@

$(INTEGRATION_BIN_DIR)/%.bin: $(INTEGRATION_BIN_DIR)/%.elf
	$(RISCV_OBJCOPY) -S -O binary $< $@

$(INTEGRATION_BIN_DIR)/%.txt: $(INTEGRATION_BIN_DIR)/%.elf
	$(RISCV_OBJDUMP) -d $< > $@

clean-test-bin:
	rm -rf $(INTEGRATION_BIN_DIR)
