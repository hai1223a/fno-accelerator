/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include "mmu.h"
#include "sim.h"
#include "common/difftest.h"

#include <cstdint>

#define __EXPORT __attribute__((visibility("default")))
#ifndef E203_DIFFTEST_ITCM_BASE
#define E203_DIFFTEST_ITCM_BASE 0x80000000u
#endif
#ifndef E203_DIFFTEST_ITCM_SIZE
#define E203_DIFFTEST_ITCM_SIZE 0x00100000u
#endif
#ifndef E203_DIFFTEST_DTCM_BASE
#define E203_DIFFTEST_DTCM_BASE 0x90000000u
#endif
#ifndef E203_DIFFTEST_DTCM_SIZE
#define E203_DIFFTEST_DTCM_SIZE 0x00100000u
#endif

using word_t = uint32_t;
using sword_t = int32_t;
using paddr_t = uint32_t;
static constexpr int NR_GPR = 32;

static std::vector<std::pair<reg_t, abstract_device_t*>> difftest_plugin_devices;
static std::vector<std::string> difftest_htif_args;
static std::vector<std::pair<reg_t, mem_t*>> difftest_mem;
static debug_module_config_t difftest_dm_config = {
  .progbufsize = 2,
  .max_sba_data_width = 0,
  .require_authentication = false,
  .abstract_rti = 0,
  .support_hasel = true,
  .support_abstract_csr_access = true,
  .support_abstract_fpr_access = true,
  .support_haltgroups = true,
  .support_impebreak = true
};

static sim_t* s = NULL;
static processor_t *p = NULL;
static state_t *state = NULL;

static void set_default_memory_layout() {
  if (!difftest_mem.empty()) {
    return;
  }
  difftest_mem.push_back(std::make_pair(reg_t(E203_DIFFTEST_ITCM_BASE),
                                        new mem_t(E203_DIFFTEST_ITCM_SIZE)));
  difftest_mem.push_back(std::make_pair(reg_t(E203_DIFFTEST_DTCM_BASE),
                                        new mem_t(E203_DIFFTEST_DTCM_SIZE)));
}

void sim_t::diff_init(int port) {
  p = get_core("0");
  state = p->get_state();
}

void sim_t::diff_step(uint64_t n) {
//执行前清空上一条指令的访存记录
  state->log_mem_read.clear();
  state->log_mem_write.clear();
  step(n);
}

void sim_t::diff_get_regs(void* diff_context) {
  auto* ctx = static_cast<e203sim::diff_context*>(diff_context);
  for (int i = 0; i < NR_GPR; i++) {
    ctx->gpr[i] = state->XPR[i];
  }
  ctx->pc = state->pc;
}

void sim_t::diff_set_regs(void* diff_context) {
  auto* ctx = static_cast<e203sim::diff_context*>(diff_context);
  for (int i = 0; i < NR_GPR; i++) {
    state->XPR.write(i, (sword_t)ctx->gpr[i]);
  }
  state->pc = ctx->pc;
}

void sim_t::diff_memcpy(reg_t dest, void* src, size_t n) {
  mmu_t* mmu = p->get_mmu();
  for (size_t i = 0; i < n; i++) {
    mmu->store<uint8_t>(dest+i, *((uint8_t*)src+i));
  }
}

extern "C" {

__EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction) {
  if (direction == e203sim::DIFFTEST_TO_REF) {
    s->diff_memcpy(addr, buf, n);
  } else {
    assert(0);
  }
}

__EXPORT void difftest_regcpy(void* dut, bool direction) {
  if (direction == e203sim::DIFFTEST_TO_REF) {
    s->diff_set_regs(dut);
  } else {
    s->diff_get_regs(dut);
  }
}

__EXPORT void difftest_getinst(void* dut) {
	mmu_t* mmu = p->get_mmu();
	word_t* diff_context_t = (word_t* )dut;
	*diff_context_t = mmu->load<uint32_t>(state->pc);
}

__EXPORT void difftest_get_store_event(void* dut) {
  auto* diff_mem_t = static_cast<e203sim::diff_store_event*>(dut);
  diff_mem_t->type  = 0;
  if(state->log_mem_write.empty())
    return;
  auto& item = state->log_mem_write.back();
  diff_mem_t->vaddr = std::get<0>(item);
  diff_mem_t->data  = std::get<1>(item);
  diff_mem_t->len   = std::get<2>(item);
  diff_mem_t->type  = 2;
}

__EXPORT void difftest_exec(uint64_t n) {
  s->diff_step(n);
}

__EXPORT void difftest_init(int port) {
  set_default_memory_layout();
  difftest_htif_args.push_back("");
  const char *isa = "RV32IM_Zicsr";
  cfg_t cfg(/*default_initrd_bounds=*/std::make_pair((reg_t)0, (reg_t)0),
            /*default_bootargs=*/nullptr,
            /*default_isa=*/isa,
            /*default_priv=*/DEFAULT_PRIV,
            /*default_varch=*/DEFAULT_VARCH,
            /*default_misaligned=*/false,
            /*default_endianness*/endianness_little,
            /*default_pmpregions=*/16,
            /*default_mem_layout=*/std::vector<mem_cfg_t>(),
            /*default_hartids=*/std::vector<size_t>(1),
            /*default_real_time_clint=*/false,
            /*default_trigger_count=*/4);
  s = new sim_t(&cfg, false,
      difftest_mem, difftest_plugin_devices, difftest_htif_args,
      difftest_dm_config, "/dev/null", false, NULL,
      false,
      NULL,
      true);
  s->configure_log(false, true);
  s->diff_init(port);
}

__EXPORT void difftest_raise_intr(uint64_t NO) {
  trap_t t(NO);
  p->take_trap_public(t, state->pc);
}

__EXPORT void difftest_set_mems(uint32_t itcm_base,
                                uint32_t itcm_size,
                                uint32_t dtcm_base,
                                uint32_t dtcm_size) {
  difftest_mem.clear();
  difftest_mem.push_back(std::make_pair(reg_t(itcm_base), new mem_t(itcm_size)));
  difftest_mem.push_back(std::make_pair(reg_t(dtcm_base), new mem_t(dtcm_size)));
}

}
