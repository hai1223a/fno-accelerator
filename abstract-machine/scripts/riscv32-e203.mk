include $(AM_HOME)/scripts/isa/riscv.mk
include $(AM_HOME)/scripts/platform/e203.mk

CFLAGS  += -DISA_H=\"riscv/riscv.h\"
COMMON_CFLAGS += -march=rv32im_zicsr 		   -mabi=ilp32  # overwrite
LDFLAGS       += -melf32lriscv                      # overwrite

