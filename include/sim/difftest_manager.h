#pragma once

#include "common/difftest.h"
#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace e203sim {

class difftest_manager
{
public:
    static difftest_manager& instance();

    void init(const sim_config& cfg, const std::vector<uint8_t>& image);
    void step(const diff_retire_event& event);
    void detach();

    bool enabled() const;
    void skip_ref(const diff_context& dut);
    void skip_dut(int nr_ref, int nr_dut);

private:
    using ref_memcpy_t = void (*)(uint32_t, void*, std::size_t, bool);
    using ref_regcpy_t = void (*)(void*, bool);
    using ref_exec_t = void (*)(uint64_t);
    using ref_raise_intr_t = void (*)(uint64_t);
    using ref_getinst_t = void (*)(void*);
    using ref_get_store_event_t = void (*)(void*);
    using ref_init_t = void (*)(int);
    using ref_set_mems_t = void (*)(uint32_t, uint32_t, uint32_t, uint32_t);

    difftest_manager() = default;
    ~difftest_manager();

    difftest_manager(const difftest_manager&) = delete;
    difftest_manager& operator=(const difftest_manager&) = delete;

    void load_symbols(const std::string& ref_so);
    void close_ref();
    void copy_regs_to_ref(const diff_context& dut);
    void check_inst(uint32_t ref_inst, const diff_retire_event& event) const;
    void check_regs(const diff_context& ref, const diff_retire_event& event) const;
    void check_store(const diff_store_event& ref, const diff_retire_event& event) const;

    void* handle_ = nullptr;
    ref_memcpy_t ref_memcpy_ = nullptr;
    ref_regcpy_t ref_regcpy_ = nullptr;
    ref_exec_t ref_exec_ = nullptr;
    ref_raise_intr_t ref_raise_intr_ = nullptr;
    ref_getinst_t ref_getinst_ = nullptr;
    ref_get_store_event_t ref_get_store_event_ = nullptr;
    ref_init_t ref_init_ = nullptr;
    ref_set_mems_t ref_set_mems_ = nullptr;

    bool enabled_ = false;
    bool check_store_ = true;
    bool is_skip_ref_ = false;
    int skip_dut_nr_inst_ = 0;
};

} // namespace e203sim

