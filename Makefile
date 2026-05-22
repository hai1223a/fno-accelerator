include scripts/mk/config.mk
include scripts/mk/build.mk
include scripts/mk/tests-dtcm.mk
include scripts/mk/tests-itcm.mk
include scripts/mk/tests-ifu.mk
include scripts/mk/tests-exu.mk
include scripts/mk/tests-biu.mk
include scripts/mk/tests-peripheral.mk
include scripts/mk/tests-integration-bin.mk
include scripts/mk/tests-pipeline.mk

.PHONY: all test
.PHONY: spike-diff
.PHONY: flamegraph

all: build

spike-diff:
	$(MAKE) -C third_party/spike-diff GUEST_ISA=riscv32

flamegraph: build
	scripts/profile_flamegraph.sh -- $(TARGET) $(ARGS)

# 分层测试入口：memory/IFU/EXU/BIU component tests 加通用 pipeline 顺序测试。
test: test-dtcm test-itcm test-ifu test-exu test-biu test-peripheral test-pipeline-stage-order

-include $(DEP)
