#include "models/core/ifu_ca.h"
#include "common/debug_logger.h"
#include "sim/pipe_trace.h"
#include "sim/thread_trace.h"

#include <algorithm>

using namespace sc_core;

namespace {

constexpr uint32_t kInstr16Mask = 0x3u;
constexpr uint32_t kOpcodeMask = 0x7fu;
constexpr uint32_t kBranchOpcode = 0x63u;
constexpr uint32_t kJalOpcode = 0x6fu;
constexpr uint32_t kJalrOpcode = 0x67u;
constexpr uint32_t kOpOpcode = 0x33u;

uint64_t load_le64(const std::array<uint8_t, 8>& data)
{
    uint64_t value = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
        value |= static_cast<uint64_t>(data[i]) << (i * 8);
    }
    return value;
}

int32_t sign_extend(uint32_t value, uint32_t bits)
{
    const uint32_t sign = 1u << (bits - 1u);
    return static_cast<int32_t>((value ^ sign) - sign);
}

e203sim::addr_t align_down(e203sim::addr_t addr, e203sim::addr_t align)
{
    return addr & ~(align - 1u);
}

} // namespace

ifu_ca::ifu_ca(sc_module_name module_name, const e203sim::sim_config& config)
    : sc_module(module_name),
      itcm_config_(config.itcm != nullptr ? *config.itcm : e203sim::memory_config{"itcm", 0, 0, 1, 1}),
      clk_period_(config.cycle_ns == 0 ? 1 : config.cycle_ns, SC_NS),
      pc_(config.reset_pc != 0 ? config.reset_pc : itcm_config_.base_addr),
      ifu2itcm_initiator_socket("ifu2itcm_initiator_socket"),
      ifu_pipeline_port("ifu_pipeline_port")
{
    if (pc_ < itcm_config_.base_addr || pc_ >= itcm_config_.base_addr + itcm_config_.size) {
        SIM_ERROR(module_name, "reset_pc must be inside ITCM while IFU->BIU fetch is not implemented: reset_pc=0x"
                               << std::hex << pc_
                               << ", itcm=[0x" << itcm_config_.base_addr
                               << ",0x" << (itcm_config_.base_addr + itcm_config_.size) << ")" << std::dec);
    }
    ifu2itcm_initiator_socket.register_nb_transport_bw(this, &ifu_ca::nb_transport_bw);
    SC_THREAD(fetch_thread);
    INFO(module_name << " created !");
}

ifu_ca::~ifu_ca() = default;

void ifu_ca::bind_pipeline(e203sim::pipeline_channel<e203sim::ifu_exu_packet, e203sim::addr_t>& pipe)
{
    ifu_pipeline_port(pipe);
}

tlm::tlm_sync_enum ifu_ca::nb_transport_bw(tlm::tlm_generic_payload& trans,
                                           tlm::tlm_phase& phase,
                                           sc_core::sc_time& delay)
{
    (void)delay;
    if (phase == tlm::BEGIN_RESP) {
        handle_response(trans);
        complete_nb_begin_resp(trans, phase, basename());
        outstanding_.reset();
        response_event_.notify(SC_ZERO_TIME);
        return tlm::TLM_COMPLETED;
    }
    return tlm::TLM_ACCEPTED;
}

void ifu_ca::issue_read32(uint32_t addr)
{
    (void)read_lane(addr);
}

void ifu_ca::fetch_thread()
{
    // 先发布一个空流水线状态。SC_ZERO_TIME 只推进 delta cycle，
    // 使 valid/packet 的初始值在真正取指前对其他模块可见。
    clear_pipeline_output();
    wait(SC_ZERO_TIME);

    while (true) {
        // 模拟 RTL 中 EXU commit -> IFU flush 的组合路径：同一时刻各线程被唤醒后，
        // IFU 先让 delta cycle，使 EXU 本周期产生的 flush/ready 信号稳定，再选择取指 PC。
        // 第二个 delta 给 long-pipe response 优先级留出空间：DTCM/LSU response 可先进入 EXU，
        // EXU 若要处理返回并暂停 dispatch，会在 IFU 弹出当前 packet 前撤销 ready。
        wait(SC_ZERO_TIME);
        wait(SC_ZERO_TIME);
        consume_pending_response();
        THREAD_TRACE("IFU取指线程", "线程唤醒", "本周期PC=0x" << std::hex << wakeup_trace_pc()
                     << std::dec << " 内部队列=" << fetch_queue_.size()
                     << " 有未处理响应=" << response_pending_
                     << " flush_epoch=" << ifu_pipeline_port->flush_epoch()
                     << " 已处理flush_epoch=" << seen_flush_epoch_);

        // flush 优先级最高：后级用 epoch 表示一次重定向事件。
        // IFU 只处理未见过的新 epoch，避免 SystemC delta 调度导致重复处理。
        // 若当前无在途取指请求，本轮直接切换到 flush PC 并尝试发起新 fetch；
        // 若已有 request 在路上，则只 ack/清本地状态，把目标 PC 记为 delayed flush，
        // 等 response 返回后补发。
        if (ifu_pipeline_port->flush_epoch() != seen_flush_epoch_) {
            seen_flush_epoch_ = ifu_pipeline_port->flush_epoch();
            delayed_flush_pending_ = false;
            delayed_flush_pc_ = ifu_pipeline_port->flush_target();
            handle_flush_request(false);
        }
        if (delayed_flush_pending_ && can_issue_fetch_request()) {
            handle_flush_request(true);
        }

        // producer 侧按“先提交上一拍握手，再准备下一拍输出”的顺序推进：
        // - ifu_pipeline_port 当前的 valid/payload 来自上一轮 drive_fetch_queue()。
        // - 前面的 delta 等待让 EXU 本周期组合出来的 ready/flush 已经稳定。
        // - 只有 fire(valid && ready) 为真才弹出队首，避免刚写 valid 就按旧 ready 误弹。
        // - 若 EXU 反压或 flush，占住队首 packet，后续 drive_fetch_queue() 会继续保持输出。
        if (ifu_pipeline_port->fire() && !fetch_queue_.empty()) {
            fetch_queue_.pop_front();
            ifu_pipeline_port->set_valid(false);
        }

        // 在提交上一拍握手之后，IFU 尝试用 hold-up lane 或新返回 lane 继续填充内部队列。
        // 队列最多 2 项，对应“当前可交付 packet + 一条 response bypass/预解候选”的 CA 抽象。
        fill_fetch_queue();
        if (fetch_queue_.empty()) {
            clear_pipeline_output();
        } else {
            drive_fetch_queue();
        }
        clear_just_received_lanes();

        wait(clk_period_, ifu_pipeline_port->flush_event());
    }
}

bool ifu_ca::can_issue_fetch_request() const
{
    return !outstanding_ && !has_nb_outstanding();
}

void ifu_ca::handle_flush_request(bool delayed)
{
    const e203sim::addr_t target_pc = delayed_flush_pc_ & ~static_cast<e203sim::addr_t>(1u);
    THREAD_TRACE("IFU取指线程", delayed ? "处理延迟冲刷" : "处理冲刷",
                 "目标PC=0x" << std::hex << target_pc
                 << " 可发新请求=" << can_issue_fetch_request() << std::dec);

    ++fetch_generation_;
    fetch_queue_.clear();
    leftover_.entries.clear();
    response_pending_ = false;
    last_muldiv_valid_ = false;
    last_muldiv_dec_ = mini_decode_result{};
    pc_ = target_pc;
    clear_pipeline_output();

    if (!can_issue_fetch_request()) {
        delayed_flush_pending_ = true;
        delayed_flush_pc_ = target_pc;
        THREAD_TRACE("IFU取指线程", "记录延迟冲刷", "目标PC=0x" << std::hex << target_pc << std::dec);
        return;
    }

    delayed_flush_pending_ = false;
}

void ifu_ca::consume_pending_response()
{
    if (!response_pending_) {
        return;
    }

    for (auto& entry : leftover_.entries) {
        if (entry.valid && entry.lane_addr == response_lane_addr_) {
            entry.lane_data = response_lane_;
            entry.status = response_status_;
            entry.just_received = true;
            THREAD_TRACE("IFU取指线程", "接收lane",
                         "lane地址=0x" << std::hex << response_lane_addr_
                         << " 长度=8 内容=0x" << response_lane_ << std::dec);
            response_pending_ = false;
            return;
        }
    }
    if (leftover_.entries.size() >= 2) {
        leftover_.entries.pop_front();
    }
    leftover_.entries.push_back(
        leftover_buffer::entry{true, response_lane_addr_, response_lane_, response_status_, true});
    THREAD_TRACE("IFU取指线程", "接收lane",
                 "lane地址=0x" << std::hex << response_lane_addr_
                 << " 长度=8 内容=0x" << response_lane_ << std::dec);
    response_pending_ = false;
}

e203sim::addr_t ifu_ca::wakeup_trace_pc() const
{
    if (!fetch_queue_.empty()) {
        return pc_;
    }

    const e203sim::addr_t lane_addr = align_down(pc_, 8u);
    if (!has_lane(lane_addr)) {
        return pc_;
    }

    const auto& entry = lookup_lane(lane_addr);
    const uint32_t byte_offset = pc_ & 0x7u;
    if (byte_offset > 4u && !has_lane(lane_addr + 8u)) {
        return pc_;
    }

    uint32_t candidate = 0;
    if (byte_offset <= 4u) {
        candidate = static_cast<uint32_t>((entry.lane_data >> (byte_offset * 8u)) & 0xffffffffull);
    } else {
        const auto& next_entry = lookup_lane(lane_addr + 8u);
        const uint32_t low = static_cast<uint32_t>((entry.lane_data >> (byte_offset * 8u)) & 0xffffull);
        const uint32_t high = static_cast<uint32_t>(next_entry.lane_data & 0xffffull);
        candidate = low | (high << 16u);
    }

    const mini_decode_result dec = mini_decode(candidate);
    const branch_prediction pred = branch_predict(pc_, dec);
    return pred.taken ? pred.target_pc : pc_ + (dec.instr_32bit ? 4u : 2u);
}

bool ifu_ca::has_lane(e203sim::addr_t lane_addr) const
{
    for (const auto& entry : leftover_.entries) {
        if (entry.valid && entry.lane_addr == lane_addr) {
            return true;
        }
    }
    return false;
}

const ifu_ca::leftover_buffer::entry& ifu_ca::lookup_lane(e203sim::addr_t lane_addr) const
{
    for (const auto& entry : leftover_.entries) {
        if (entry.valid && entry.lane_addr == lane_addr) {
            return entry;
        }
    }
    SIM_ASSERT(false, "IFU hold-up lane lookup missed after has_lane check");
    return leftover_.entries.front();
}

void ifu_ca::request_lane(e203sim::addr_t pc, e203sim::addr_t lane_addr)
{
    if (outstanding_ || has_nb_outstanding()) {
        return;
    }
    THREAD_TRACE("IFU取指线程", "访问ITCM",
                 "PC=0x" << std::hex << pc
                 << " lane地址=0x" << lane_addr
                 << " 长度=8" << std::dec);
    send_read64(make_read64(lane_addr));
}

bool ifu_ca::try_make_candidate(e203sim::addr_t pc,
                                uint32_t& candidate,
                                bool& bus_error,
                                bool& from_new_lane,
                                e203sim::addr_t& lane_addr,
                                e203sim::addr_t& next_lane_addr)
{
    candidate_bus_error_ = false;
    from_new_lane = false;
    lane_addr = align_down(pc, 8u);
    next_lane_addr = 0;
    if (!has_lane(lane_addr)) {
        request_lane(pc, lane_addr);
        return false;
    }

    const auto& entry = lookup_lane(lane_addr);
    const uint64_t lane = entry.lane_data;
    const uint32_t byte_offset = pc & 0x7u;
    bus_error = entry.status != tlm::TLM_OK_RESPONSE;
    if (byte_offset <= 4u) {
        candidate = static_cast<uint32_t>((lane >> (byte_offset * 8u)) & 0xffffffffull);
        from_new_lane = entry.just_received;
        return true;
    }

    next_lane_addr = lane_addr + 8u;
    if (!has_lane(next_lane_addr)) {
        request_lane(pc, next_lane_addr);
        return false;
    }

    const auto& next_entry = lookup_lane(next_lane_addr);
    const uint64_t next_lane = next_entry.lane_data;
    const uint32_t low = static_cast<uint32_t>((lane >> (byte_offset * 8u)) & 0xffffull);
    const uint32_t high = static_cast<uint32_t>(next_lane & 0xffffull);
    candidate = low | (high << 16u);
    bus_error = bus_error || next_entry.status != tlm::TLM_OK_RESPONSE;
    from_new_lane = entry.just_received || next_entry.just_received;
    return true;
}

void ifu_ca::clear_just_received_lanes()
{
    for (auto& entry : leftover_.entries) {
        entry.just_received = false;
    }
}

void ifu_ca::fill_fetch_queue()
{
    // 队列深度 2 对应 IFU IR 寄存器加 response bypass：当前给 EXU 的一条，
    // 以及已经由 hold-up/fake response 准备好的下一条。
    while (fetch_queue_.size() < 2) {
        const e203sim::addr_t fetch_pc = pc_;
        uint32_t candidate = 0;
        bool bus_error = false;
        bool from_new_lane = false;
        e203sim::addr_t lane_addr = 0;
        e203sim::addr_t next_lane_addr = 0;
        if (!try_make_candidate(fetch_pc, candidate, bus_error, from_new_lane, lane_addr, next_lane_addr)) {
            break;
        }

        const bool misalign = (fetch_pc & 0x1u) != 0u;
        const mini_decode_result dec = mini_decode(candidate);
        const branch_prediction pred = branch_predict(fetch_pc, dec);
        auto packet = build_packet(fetch_pc, candidate, dec, pred, bus_error, misalign);
        if (e203sim::pipe_trace::instance().enabled()) {
            packet.trace_fetch_tick = allocate_trace_fetch_tick();
        }
        fetch_queue_.push_back(packet);
        update_muldiv_b2b_state(dec);
        const bool cross_lane = next_lane_addr != 0;
        if (cross_lane) {
            THREAD_TRACE("IFU取指线程", from_new_lane ? "提取ITCM指令" : "命中lane",
                         "PC=0x" << std::hex << fetch_pc << " 指令=0x" << packet.instr
                         << " lane地址=0x" << lane_addr << "/0x" << next_lane_addr << std::dec);
        } else {
            THREAD_TRACE("IFU取指线程", from_new_lane ? "提取ITCM指令" : "命中lane",
                         "PC=0x" << std::hex << fetch_pc << " 指令=0x" << packet.instr
                         << " lane地址=0x" << lane_addr << std::dec);
        }

        if (pred.taken) {
            pc_ = pred.target_pc;
        } else {
            pc_ = fetch_pc + (dec.instr_32bit ? 4u : 2u);
        }

        if (from_new_lane) {
            break;
        }
    }
}

uint64_t ifu_ca::allocate_trace_fetch_tick()
{
    auto& trace = e203sim::pipe_trace::instance();
    const uint64_t now = trace.now_tick();
    const uint64_t tick = std::max(now, next_trace_fetch_tick_);
    next_trace_fetch_tick_ = tick + trace.cycle_tick();
    return tick;
}

uint64_t ifu_ca::read_lane(e203sim::addr_t addr)
{
    // IFU 到 ITCM 是 req/resp 分离的 non-blocking TLM。
    // 这里发出 BEGIN_REQ 后挂起线程，直到 nb_transport_bw 收到 BEGIN_RESP 并 notify。
    send_read64(make_read64(addr));
    wait(response_event_);
    return response_lane_;
}

uint64_t ifu_ca::get_lane(e203sim::addr_t lane_addr)
{
    // leftover 命中表示当前 PC 仍落在上一条 64-bit fetch lane 中，
    // 可以直接复用而不产生新的 ITCM transaction。
    if (has_lane(lane_addr)) {
        return lookup_lane(lane_addr).lane_data;
    }

    // miss 时访问 ITCM，并把返回 lane 存为新的 leftover。
    // 跨 lane 指令读取下一条 lane 后，下一条 lane 也会成为后续 fetch 的缓存。
    const uint64_t lane = read_lane(lane_addr);
    candidate_bus_error_ = candidate_bus_error_ || response_status_ != tlm::TLM_OK_RESPONSE;
    response_lane_addr_ = lane_addr;
    response_lane_ = lane;
    response_pending_ = true;
    consume_pending_response();
    return lane;
}

uint32_t ifu_ca::make_candidate(e203sim::addr_t pc)
{
    // 每条候选指令重新累计 bus error；跨 lane 时任一 lane 出错都会标记错误。
    candidate_bus_error_ = false;
    const e203sim::addr_t lane_addr = align_down(pc, 8u);
    const uint64_t lane = get_lane(lane_addr);
    const uint32_t byte_offset = pc & 0x7u;

    // offset <= 4 时，当前 64-bit lane 内至少还剩 32-bit，可直接截取候选值。
    if (byte_offset <= 4u) {
        return static_cast<uint32_t>((lane >> (byte_offset * 8u)) & 0xffffffffull);
    }

    // offset == 6 时，32-bit 候选值跨两个 64-bit lane：
    // 低 16-bit 来自当前 lane，高 16-bit 来自下一 lane。
    const uint64_t next_lane = get_lane(lane_addr + 8u);
    const uint32_t low = static_cast<uint32_t>((lane >> (byte_offset * 8u)) & 0xffffull);
    const uint32_t high = static_cast<uint32_t>(next_lane & 0xffffull);
    return low | (high << 16u);
}

ifu_ca::mini_decode_result ifu_ca::mini_decode(uint32_t instr) const
{
    mini_decode_result dec;
    // RISC-V C 扩展约定：低两位不是 11 表示 16-bit 压缩指令。
    dec.instr_32bit = (instr & kInstr16Mask) == kInstr16Mask;

    if (!dec.instr_32bit) {
        return dec;
    }

    dec.rs1idx = (instr >> 15u) & 0x1fu;
    dec.rs2idx = (instr >> 20u) & 0x1fu;
    dec.rdidx = (instr >> 7u) & 0x1fu;

    const uint32_t opcode = instr & kOpcodeMask;
    dec.branch = opcode == kBranchOpcode;
    dec.jal = opcode == kJalOpcode;
    dec.jalr = opcode == kJalrOpcode;
    if (opcode == kOpOpcode && ((instr >> 25u) & 0x7fu) == 0x01u) {
        const uint32_t funct3 = (instr >> 12u) & 0x7u;
        dec.mul = funct3 == 0x0u;
        dec.mulh = funct3 == 0x1u;
        dec.mulhsu = funct3 == 0x2u;
        dec.mulhu = funct3 == 0x3u;
        dec.div = funct3 == 0x4u;
        dec.divu = funct3 == 0x5u;
        dec.rem = funct3 == 0x6u;
        dec.remu = funct3 == 0x7u;
    }

    // 这里只解出 IFU 预测需要的跳转立即数，不做完整指令译码。
    if (dec.branch) {
        const uint32_t imm = ((instr >> 31u) & 0x1u) << 12u |
                             ((instr >> 7u) & 0x1u) << 11u |
                             ((instr >> 25u) & 0x3fu) << 5u |
                             ((instr >> 8u) & 0xfu) << 1u;
        dec.bjp_imm = sign_extend(imm, 13u);
    } else if (dec.jal) {
        const uint32_t imm = ((instr >> 31u) & 0x1u) << 20u |
                             ((instr >> 12u) & 0xffu) << 12u |
                             ((instr >> 20u) & 0x1u) << 11u |
                             ((instr >> 21u) & 0x3ffu) << 1u;
        dec.bjp_imm = sign_extend(imm, 21u);
    } else if (dec.jalr) {
        dec.bjp_imm = sign_extend((instr >> 20u) & 0xfffu, 12u);
    }

    return dec;
}

bool ifu_ca::is_muldiv_b2b(const mini_decode_result& dec) const
{
    if (!last_muldiv_valid_) {
        return false;
    }

    const bool pair_match =
        (dec.mul && (last_muldiv_dec_.mulh || last_muldiv_dec_.mulhsu || last_muldiv_dec_.mulhu)) ||
        (dec.div && last_muldiv_dec_.rem) ||
        (dec.rem && last_muldiv_dec_.div) ||
        (dec.divu && last_muldiv_dec_.remu) ||
        (dec.remu && last_muldiv_dec_.divu);
    const bool same_sources = dec.rs1idx == last_muldiv_dec_.rs1idx &&
                              dec.rs2idx == last_muldiv_dec_.rs2idx;
    const bool sources_not_last_rd = dec.rs1idx != last_muldiv_dec_.rdidx &&
                                     dec.rs2idx != last_muldiv_dec_.rdidx;
    return pair_match && same_sources && sources_not_last_rd;
}

void ifu_ca::update_muldiv_b2b_state(const mini_decode_result& dec)
{
    const bool is_muldiv = dec.mul || dec.mulh || dec.mulhsu || dec.mulhu ||
                           dec.div || dec.divu || dec.rem || dec.remu;
    if (!is_muldiv) {
        last_muldiv_valid_ = false;
        last_muldiv_dec_ = mini_decode_result{};
        return;
    }

    last_muldiv_valid_ = true;
    last_muldiv_dec_ = dec;
}

ifu_ca::branch_prediction ifu_ca::branch_predict(e203sim::addr_t pc, const mini_decode_result& dec) const
{
    branch_prediction pred;
    pred.target_pc = pc + static_cast<e203sim::addr_t>(dec.instr_32bit ? 4u : 2u);

    // CA 模型先采用轻量静态预测：无条件跳转 taken，条件分支只预测后跳 taken。
    if (dec.jal || dec.jalr) {
        pred.taken = true;
        pred.target_pc = pc + static_cast<e203sim::addr_t>(dec.bjp_imm);
    } else if (dec.branch && dec.bjp_imm < 0) {
        pred.taken = true;
        pred.target_pc = pc + static_cast<e203sim::addr_t>(dec.bjp_imm);
    }

    return pred;
}

e203sim::ifu_exu_packet ifu_ca::build_packet(e203sim::addr_t pc,
                                             uint32_t instr,
                                             const mini_decode_result& dec,
                                             const branch_prediction& pred,
                                             bool bus_error,
                                             bool misalign) const
{
    e203sim::ifu_exu_packet packet;
    packet.pc = pc;
    packet.instr = dec.instr_32bit ? instr : (instr & 0xffffu);
    packet.instr_32bit = dec.instr_32bit;
    packet.pc_valid = !bus_error && !misalign;
    packet.bus_error = bus_error;
    packet.misalign = misalign;
    packet.rs1idx = dec.rs1idx;
    packet.rs2idx = dec.rs2idx;
    packet.prdt_taken = pred.taken;
    packet.muldiv_b2b = is_muldiv_b2b(dec);
    if (e203sim::pipe_trace::instance().enabled()) {
        packet.trace_seq = e203sim::pipe_trace::instance().next_seq();
    }
    return packet;
}

void ifu_ca::drive_fetch_queue()
{
    // valid 与 packet 同步由 producer 写入通用流水线 channel。
    ifu_pipeline_port->set_valid(!fetch_queue_.empty());
    if (!fetch_queue_.empty()) {
        ifu_pipeline_port->write(fetch_queue_.front());
    }
}

void ifu_ca::clear_pipeline_output()
{
    // 清输出时同时写一个空 packet，避免后级误读旧数据。
    ifu_pipeline_port->set_valid(false);
    ifu_pipeline_port->write(e203sim::ifu_exu_packet{});
}

void ifu_ca::send_read64(std::unique_ptr<trans_ctx> ctx)
{
    SIM_ASSERT(!outstanding_ && !has_nb_outstanding(), "IFU只允许一个outstanding取指事务");

    // 保存 ctx 后再发 BEGIN_REQ，保证 TLM payload 和 data buffer 活到响应结束。
    outstanding_ = std::move(ctx);
    send_nb_begin_req(ifu2itcm_initiator_socket, outstanding_->trans, basename(), "read64 ITCM lane");
}

void ifu_ca::handle_response(tlm::tlm_generic_payload& trans)
{
    SIM_ASSERT(outstanding_, "IFU outstanding是空的，但是收到了响应");
    // 响应阶段只记录结果并唤醒 fetch_thread；是否标记 bus_error 由 make_candidate 汇总。
    response_status_ = trans.get_response_status();
    response_lane_ = load_le64(outstanding_->data);
    response_lane_addr_ = outstanding_->addr;
    response_pending_ = outstanding_->generation == fetch_generation_;

    if (trans.is_response_error()) {
        SIM_INFO(basename(), "ifu2itcm响应错误: status=" << trans.get_response_string());
    } else {
        SIM_INFO(basename(), "收到取指响应, lane64=0x" << std::hex << response_lane_
                             << ", addr=0x" << outstanding_->addr << std::dec);
    }

}

std::unique_ptr<ifu_ca::trans_ctx> ifu_ca::make_read64(e203sim::addr_t addr)
{
    auto ctx = std::make_unique<trans_ctx>();
    ctx->addr = addr;
    ctx->generation = fetch_generation_;
    ctx->trans.set_command(tlm::TLM_READ_COMMAND);
    ctx->trans.set_address(addr);
    ctx->trans.set_data_ptr(ctx->data.data());
    ctx->trans.set_data_length(8);
    ctx->trans.set_streaming_width(8);
    ctx->trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
    return ctx;
}
