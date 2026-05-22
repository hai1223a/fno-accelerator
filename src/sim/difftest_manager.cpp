#include "sim/difftest_manager.h"

#include "common/debug_logger.h"

#include <dlfcn.h>

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::string hex32(uint32_t value)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
    return oss.str();
}

template <typename T>
T load_required_symbol(void* handle, const char* name)
{
    dlerror();
    void* symbol = dlsym(handle, name);
    const char* err = dlerror();
    if (err != nullptr || symbol == nullptr) {
        std::cerr << "difftest missing symbol " << name << ": "
                  << (err != nullptr ? err : "null") << std::endl;
        std::exit(1);
    }
    return reinterpret_cast<T>(symbol);
}

template <typename T>
T load_optional_symbol(void* handle, const char* name)
{
    dlerror();
    void* symbol = dlsym(handle, name);
    const char* err = dlerror();
    if (err != nullptr || symbol == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(symbol);
}

[[noreturn]] void diff_abort(const std::string& msg)
{
    std::cerr << "[DIFFTEST] " << msg << std::endl;
    std::abort();
}

} // namespace

namespace e203sim {

difftest_manager& difftest_manager::instance()
{
    static difftest_manager manager;
    return manager;
}

difftest_manager::~difftest_manager()
{
    close_ref();
}

bool difftest_manager::enabled() const
{
    return enabled_;
}

void difftest_manager::load_symbols(const std::string& ref_so)
{
    handle_ = dlopen(ref_so.c_str(), RTLD_LAZY);
    if (handle_ == nullptr) {
        std::cerr << "cannot open difftest reference: " << ref_so
                  << ": " << dlerror() << std::endl;
        std::exit(1);
    }

    ref_memcpy_ = load_required_symbol<ref_memcpy_t>(handle_, "difftest_memcpy");
    ref_regcpy_ = load_required_symbol<ref_regcpy_t>(handle_, "difftest_regcpy");
    ref_exec_ = load_required_symbol<ref_exec_t>(handle_, "difftest_exec");
    ref_raise_intr_ = load_required_symbol<ref_raise_intr_t>(handle_, "difftest_raise_intr");
    ref_getinst_ = load_required_symbol<ref_getinst_t>(handle_, "difftest_getinst");
    ref_get_store_event_ = load_required_symbol<ref_get_store_event_t>(handle_, "difftest_get_store_event");
    ref_init_ = load_required_symbol<ref_init_t>(handle_, "difftest_init");
    ref_set_mems_ = load_optional_symbol<ref_set_mems_t>(handle_, "difftest_set_mems");
}

void difftest_manager::close_ref()
{
    if (handle_ != nullptr) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    ref_memcpy_ = nullptr;
    ref_regcpy_ = nullptr;
    ref_exec_ = nullptr;
    ref_raise_intr_ = nullptr;
    ref_getinst_ = nullptr;
    ref_get_store_event_ = nullptr;
    ref_init_ = nullptr;
    ref_set_mems_ = nullptr;
    enabled_ = false;
}

void difftest_manager::init(const sim_config& cfg, const std::vector<uint8_t>& image)
{
    close_ref();
    if (!cfg.enable_difftest) {
        return;
    }
    if (cfg.difftest_ref_so.empty()) {
        std::cerr << "enable_difftest requires difftest_ref_so" << std::endl;
        std::exit(1);
    }
    if (cfg.itcm == nullptr || cfg.dtcm == nullptr) {
        std::cerr << "difftest requires itcm and dtcm memory config" << std::endl;
        std::exit(1);
    }

    check_store_ = cfg.difftest_check_store;
    load_symbols(cfg.difftest_ref_so);
    if (ref_set_mems_ != nullptr) {
        ref_set_mems_(cfg.itcm->base_addr, cfg.itcm->size, cfg.dtcm->base_addr, cfg.dtcm->size);
    }
    ref_init_(0);

    if (!image.empty()) {
        auto* bytes = const_cast<uint8_t*>(image.data());
        ref_memcpy_(cfg.load_addr, bytes, image.size(), DIFFTEST_TO_REF);
    }

    diff_context initial;
    initial.pc = cfg.reset_pc;
    copy_regs_to_ref(initial);

    enabled_ = true;
    SIM_INFO("difftest", "ON ref=" << cfg.difftest_ref_so
                          << " image_size=0x" << std::hex << image.size() << std::dec);
}

void difftest_manager::detach()
{
    enabled_ = false;
}

void difftest_manager::copy_regs_to_ref(const diff_context& dut)
{
    diff_context copy = dut;
    ref_regcpy_(&copy, DIFFTEST_TO_REF);
}

void difftest_manager::skip_ref(const diff_context& dut)
{
    if (!enabled_) {
        return;
    }
    copy_regs_to_ref(dut);
    is_skip_ref_ = false;
    skip_dut_nr_inst_ = 0;
}

void difftest_manager::skip_dut(int nr_ref, int nr_dut)
{
    if (!enabled_) {
        return;
    }
    skip_dut_nr_inst_ += nr_dut;
    while (nr_ref-- > 0) {
        ref_exec_(1);
    }
}

void difftest_manager::check_inst(uint32_t ref_inst, const diff_retire_event& event) const
{
    if (ref_inst != event.instr) {
        diff_abort("inst mismatch at pc=" + hex32(event.pc) +
                   ", dut=" + hex32(event.instr) +
                   ", ref=" + hex32(ref_inst));
    }
}

void difftest_manager::check_regs(const diff_context& ref, const diff_retire_event& event) const
{
    if (ref.pc != event.next_pc) {
        diff_abort("PC mismatch at pc=" + hex32(event.pc) +
                   ", dut=" + hex32(event.next_pc) +
                   ", ref=" + hex32(ref.pc));
    }
    for (std::size_t i = 0; i < event.regs.gpr.size(); ++i) {
        if (ref.gpr[i] != event.regs.gpr[i]) {
            diff_abort("GPR mismatch at pc=" + hex32(event.pc) +
                       ", x" + std::to_string(i) +
                       ", dut=" + hex32(event.regs.gpr[i]) +
                       ", ref=" + hex32(ref.gpr[i]));
        }
    }
}

void difftest_manager::check_store(const diff_store_event& ref, const diff_retire_event& event) const
{
    if (!check_store_ || ref.type != 2) {
        return;
    }
    if (!event.has_store || event.store.type != ref.type) {
        diff_abort("store type mismatch at pc=" + hex32(event.pc));
    }
    if (ref.vaddr != event.store.vaddr) {
        diff_abort("store addr mismatch at pc=" + hex32(event.pc) +
                   ", dut=" + hex32(event.store.vaddr) +
                   ", ref=" + hex32(ref.vaddr));
    }
    if (ref.data != event.store.data) {
        diff_abort("store data mismatch at pc=" + hex32(event.pc) +
                   ", dut=" + hex32(event.store.data) +
                   ", ref=" + hex32(ref.data));
    }
    if (ref.len != event.store.len) {
        diff_abort("store len mismatch at pc=" + hex32(event.pc) +
                   ", dut=" + std::to_string(event.store.len) +
                   ", ref=" + std::to_string(ref.len));
    }
}

void difftest_manager::step(const diff_retire_event& event)
{
    if (!enabled_) {
        return;
    }

    if (event.skip_ref || is_skip_ref_) {
        if (!event.skip_reason.empty()) {
            INFO("[DIFFTEST] skip ref at pc=" << hex32(event.pc)
                 << " reason=" << event.skip_reason);
        }
        copy_regs_to_ref(event.regs);
        is_skip_ref_ = false;
        skip_dut_nr_inst_ = 0;
        return;
    }

    if (skip_dut_nr_inst_ > 0) {
        diff_context ref;
        ref_regcpy_(&ref, DIFFTEST_TO_DUT);
        if (ref.pc == event.next_pc) {
            skip_dut_nr_inst_ = 0;
            check_regs(ref, event);
            return;
        }
        --skip_dut_nr_inst_;
        if (skip_dut_nr_inst_ == 0) {
            diff_abort("can not catch up with ref.pc=" + hex32(ref.pc) +
                       " at pc=" + hex32(event.pc));
        }
        return;
    }

    uint32_t ref_inst = 0;
    diff_context ref;
    diff_store_event ref_store;
    ref_getinst_(&ref_inst);
    ref_exec_(1);
    ref_regcpy_(&ref, DIFFTEST_TO_DUT);
    ref_get_store_event_(&ref_store);

    check_inst(ref_inst, event);
    check_regs(ref, event);
    check_store(ref_store, event);
}

} // namespace e203sim

