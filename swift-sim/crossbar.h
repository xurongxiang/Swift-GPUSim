//
// Created by root on 22-10-25.
//

#ifndef CROSSBAR_H
#define CROSSBAR_H

#include <iostream>
#include <map>
#include <queue>
#include <vector>


enum Interconnect_type { REQ_NET = 0, REPLY_NET = 1 };

enum Arbiteration_type { NAIVE_RR = 0 };

struct inct_config {
    // config for local interconnect
    unsigned in_buffer_limit;
    unsigned out_buffer_limit;
    unsigned subnets;
    Arbiteration_type arbiter_algo;
};

class xbar_router {
public:
    xbar_router(unsigned router_id, enum Interconnect_type m_type,
                unsigned n_shader, unsigned n_mem,
                const struct inct_config& m_localinct_config);
    ~xbar_router();
    void Push(unsigned input_deviceID, unsigned output_deviceID, void* data,
              unsigned int size);
    void* Pop(unsigned ouput_deviceID);
    void Advance();

    bool Busy() const;
    bool Has_Buffer_In(unsigned input_deviceID, unsigned size);
    bool Has_Buffer_Out(unsigned output_deviceID, unsigned size);

private:
    void RR_Advance();

    struct Packet {
        Packet(void* m_data, unsigned m_output_deviceID) {
            data = m_data;
            output_deviceID = m_output_deviceID;
        }
        void* data;
        unsigned output_deviceID;
    };
    std::vector<std::queue<Packet> > in_buffers;
    std::vector<std::queue<Packet> > out_buffers;
    unsigned _n_shader, _n_mem, total_nodes;
    unsigned in_buffer_limit, out_buffer_limit;
    unsigned next_node_id;       // used for RR arbit
    unsigned m_id;
    enum Interconnect_type router_type;
    unsigned active_in_buffers, active_out_buffers;
    Arbiteration_type arbit_type;

    friend class LocalInterconnect;

    unsigned long long cycles;
    unsigned long long conflicts_util;
    unsigned long long conflicts;
    unsigned long long cycles_util;
};

class LocalInterconnect {
public:
    LocalInterconnect(const struct inct_config& m_localinct_config);
    ~LocalInterconnect();
    static LocalInterconnect* New(const struct inct_config& m_inct_config);
    void CreateInterconnect(unsigned n_shader, unsigned n_mem);

    // node side functions
    void Init();
    void Push(unsigned input_deviceID, unsigned output_deviceID, void* data,
              unsigned int size);
    void* Pop(unsigned ouput_deviceID);
    void Advance();
    bool Busy() const;
    bool HasBuffer(unsigned deviceID, unsigned int size) const;
    void DisplayStats();

protected:
    const inct_config& m_inct_config;
    unsigned n_shader, n_mem;
    unsigned n_subnets;
    std::vector<xbar_router*> net;

};

#endif
