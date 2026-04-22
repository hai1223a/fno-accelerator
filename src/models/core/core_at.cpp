#include "models/core/core_at.h" 
#include <iostream>
using namespace std;
using namespace sc_core;

core_at::core_at(sc_module_name module_name)
    :sc_module(module_name)
{
    SC_THREAD(start);
    cout << module_name << " created !" << endl;
}

core_at::~core_at(){}

void core_at::start()
{
    // auto send_read = [](auto& socket, sc_dt::uint64 addr, const char* port_name) {
    //     tlm::tlm_generic_payload trans;
    //     sc_time delay = SC_ZERO_TIME;
    //     uint32_t data = 0;

    //     trans.set_command(tlm::TLM_READ_COMMAND);
    //     trans.set_address(addr);
    //     trans.set_data_ptr(reinterpret_cast<unsigned char*>(&data));
    //     trans.set_data_length(sizeof(data));
    //     trans.set_streaming_width(sizeof(data));
    //     trans.set_byte_enable_ptr(nullptr);
    //     trans.set_dmi_allowed(false);
    //     trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);

    //     cout << "[" << sc_time_stamp() << "] "
    //          << "core_at send read via " << port_name
    //          << " addr=0x" << hex << addr << dec
    //          << endl;

    //     socket->b_transport(trans, delay);

    //     if (delay != SC_ZERO_TIME) {
    //         wait(delay);
    //     }

    //     cout << "[" << sc_time_stamp() << "] "
    //          << "core_at got response via " << port_name
    //          << " status=" << trans.get_response_string()
    //          << endl;
    // };

    // send_read(core2itlm_initiator_socket, 0x80000000, "itlm");
    // send_read(core2dtlm_initiator_socket, 0x90000000, "dtlm");
    // send_read(core2biu_initiator_socket, 0x10000000, "biu");

    sc_stop();
}
