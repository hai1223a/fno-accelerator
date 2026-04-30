include scripts/mk/config.mk
include scripts/mk/build.mk
include scripts/mk/tests-dtcm.mk
include scripts/mk/tests-itcm.mk

.PHONY: all test

all: build

test: test-dtcm test-itcm

-include $(DEP)
