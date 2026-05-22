AM_SRCS :=  riscv/e203/start.S \
            riscv/e203/trap.S \
            riscv/e203/cte.c \
            riscv/e203/ioe.c \
            riscv/e203/mpe.c \
            riscv/e203/timer.c \
            riscv/e203/trm.c \
            riscv/e203/vme.c

CFLAGS    += -fdata-sections -ffunction-sections
# CFLAGS    += -I$(AM_HOME)/am/src/platform/nemu/include
LDSCRIPTS += $(AM_HOME)/scripts/linker_e203.ld
LDFLAGS   += --gc-sections -e _start

IMAGE_NAME = $(basename $(notdir $(IMAGE)))
override E203FLAGS += -c $(YSYX_HOME)/configs/e203sim.json \
					  -i $(IMAGE).bin

MAINARGS_MAX_LEN = 64
MAINARGS_PLACEHOLDER = the_insert-arg_rule_in_Makefile_will_insert_mainargs_here
CFLAGS += -DMAINARGS_MAX_LEN=$(MAINARGS_MAX_LEN) -DMAINARGS_PLACEHOLDER=$(MAINARGS_PLACEHOLDER)

insert-arg: image
	@file -b $(IMAGE).bin | grep -q data || echo "ERROR: $(IMAGE).bin is not binary(data)"
# 	hexdump -C -s 0xd8 -n 96 $(IMAGE).bin
	@python $(AM_HOME)/tools/insert-arg.py $(IMAGE).bin $(MAINARGS_MAX_LEN) $(MAINARGS_PLACEHOLDER) "$(mainargs)"
# 	hexdump -C -s 0xd8 -n 96 $(IMAGE).bin
image: image-dep
	@$(OBJDUMP) -d $(IMAGE).elf > $(IMAGE).txt
	@echo + OBJCOPY "->" $(IMAGE_REL).bin
# 	$(OBJDUMP) -s -j .rodata $(IMAGE).elf
	@$(OBJCOPY) -S --set-section-flags .bss=alloc,contents -O binary $(IMAGE).elf $(IMAGE).bin

run: insert-arg
	@$(MAKE) -C $(YSYX_HOME) run ARGS="$(E203FLAGS)"

.PHONY: insert-arg
