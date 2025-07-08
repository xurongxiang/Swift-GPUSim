//
// Created by root on 22-10-25. 
//
// The NoC module in Swift-Sim reuses some code from gpgpu-sim.
// Accel-Sim : https://github.com/accel-sim/accel-sim-framework.git
// gpgpu-sim : https://github.com/gpgpu-sim/gpgpu-sim_distribution.git

#include "crossbar.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <utility>

#include "mem_fetch.h"

xbar_router::xbar_router(unsigned router_id, enum Interconnect_type m_type,
                         unsigned n_shader, unsigned n_mem,
                         const struct inct_config& m_localinct_config) {
    m_id = router_id;
    router_type = m_type;
    _n_mem = n_mem;
    _n_shader = n_shader;
    total_nodes = n_shader + n_mem;
    in_buffers.resize(total_nodes);
    out_buffers.resize(total_nodes);
    in_buffer_limit = m_localinct_config.in_buffer_limit;
    out_buffer_limit = m_localinct_config.out_buffer_limit;
    arbit_type = m_localinct_config.arbiter_algo;
    next_node_id = 0;
    if (m_type == REQ_NET) {
        active_in_buffers = n_shader;
        active_out_buffers = n_mem;
    } else if (m_type == REPLY_NET) {
        active_in_buffers = n_mem;
        active_out_buffers = n_shader;
    }
    cycles = 0;
    cycles_util = 0;
    conflicts_util = 0;
    conflicts = 0;
}

xbar_router::~xbar_router() {}

void xbar_router::Push(unsigned input_deviceID, unsigned output_deviceID,
                       void* data, unsigned int size) {
    in_buffers[input_deviceID].push(Packet(data, output_deviceID));
}

void* xbar_router::Pop(unsigned ouput_deviceID) {
    void* data = NULL;
    if (!out_buffers[ouput_deviceID].empty()) {
        data = out_buffers[ouput_deviceID].front().data;
        out_buffers[ouput_deviceID].pop();
    }
    return data;
}

bool xbar_router::Has_Buffer_In(unsigned input_deviceID, unsigned size) {
    bool has_buffer =
            (in_buffers[input_deviceID].size() + size <= in_buffer_limit);
    return has_buffer;
}

bool xbar_router::Has_Buffer_Out(unsigned output_deviceID, unsigned size) {
    return (out_buffers[output_deviceID].size() + size <= out_buffer_limit);
}

void xbar_router::Advance() {
    if (arbit_type == NAIVE_RR)
        RR_Advance();
    else
       abort();
}

void xbar_router::RR_Advance() {
    bool active = false;
    std::vector<bool> issued(total_nodes, false);
    unsigned conflict_sub = 0;
    unsigned reqs = 0;

    for (unsigned i = 0; i < total_nodes; ++i) {
        unsigned node_id = (i + next_node_id) % total_nodes;

        if (!in_buffers[node_id].empty()) {
            active = true;
            Packet _packet = in_buffers[node_id].front();

            if (Has_Buffer_Out(_packet.output_deviceID, 1)) {
                if (!issued[_packet.output_deviceID]) {
                    out_buffers[_packet.output_deviceID].push(_packet);
                    in_buffers[node_id].pop();
                    issued[_packet.output_deviceID] = true;
                    reqs++;
                } else
                    conflict_sub++;
            } else {
                if (issued[_packet.output_deviceID]) conflict_sub++;
            }
        }
    }
    next_node_id = (++next_node_id % total_nodes);

    conflicts += conflict_sub;
    if (active) {
        conflicts_util += conflict_sub;
        cycles_util++;
    }
    cycles++;
}

bool xbar_router::Busy() const {
    for (unsigned i = 0; i < total_nodes; ++i) {
        if (!in_buffers[i].empty()) return true;

        if (!out_buffers[i].empty()) return true;
    }
    return false;
}


// assume all the packets are one flit
#define LOCAL_INCT_FLIT_SIZE 40

LocalInterconnect* LocalInterconnect::New(
        const struct inct_config& m_localinct_config) {
    auto* icnt_interface = new LocalInterconnect(m_localinct_config);

    return icnt_interface;
}

LocalInterconnect::LocalInterconnect(
        const struct inct_config& m_localinct_config)
        : m_inct_config(m_localinct_config) {
    n_shader = 0;
    n_mem = 0;
    n_subnets = m_localinct_config.subnets;
}

LocalInterconnect::~LocalInterconnect() {
    for (unsigned i = 0; i < m_inct_config.subnets; ++i) {
        delete net[i];
    }
}

void LocalInterconnect::CreateInterconnect(unsigned m_n_shader,
                                           unsigned m_n_mem) {
    n_shader = m_n_shader;
    n_mem = m_n_mem;

    net.resize(n_subnets);
    for (unsigned i = 0; i < n_subnets; ++i) {
        net[i] = new xbar_router(i, static_cast<Interconnect_type>(i), m_n_shader,
                                 m_n_mem, m_inct_config);
    }
}

void LocalInterconnect::Init() {
    // empty
    // there is nothing to do
}

void LocalInterconnect::Push(unsigned input_deviceID, unsigned output_deviceID,
                             void* data, unsigned int size) {
    unsigned subnet;
    if (n_subnets == 1) {
        subnet = 0;
    } else {
        if (input_deviceID < n_shader) {
            subnet = 0;
        } else {
            subnet = 1;
        }
    }
    net[subnet]->Push(input_deviceID, output_deviceID, data, size);
}

void* LocalInterconnect::Pop(unsigned ouput_deviceID) {
    // 0-_n_shader-1 indicates reply(network 1), otherwise request(network 0)
    int subnet = 0;
    if (ouput_deviceID < n_shader) subnet = 1;

    return net[subnet]->Pop(ouput_deviceID);
}

void LocalInterconnect::Advance() {
    for (unsigned i = 0; i < n_subnets; ++i) {
        net[i]->Advance();
    }
}

bool LocalInterconnect::Busy() const {
    for (unsigned i = 0; i < n_subnets; ++i) {
        if (net[i]->Busy()) return true;
    }
    return false;
}

bool LocalInterconnect::HasBuffer(unsigned deviceID, unsigned int size) const {
    bool has_buffer = false;
    if ((n_subnets > 1) && deviceID >= n_shader)  // deviceID is memory node
        has_buffer = net[REPLY_NET]->Has_Buffer_In(deviceID, 1);
    else
        has_buffer = net[REQ_NET]->Has_Buffer_In(deviceID, 1);
    return has_buffer;
}

void LocalInterconnect::DisplayStats(){
    for(unsigned i = 0; i < n_subnets; i++){
        printf("LocalInterconnect cycles net[%u] = %llu\n", i, net[i]->cycles);
        printf("LocalInterconnect conflicts_per_cycle net[%u] =  %12.4f\n", i,
               (float)(net[i]->conflicts) / (net[i]->cycles));
        printf("LocalInterconnect conflicts_per_cycle_util net[%u] = %12.4f\n", i,
               (float)(net[i]->conflicts_util) / (net[i]->cycles_util));
    }


}



