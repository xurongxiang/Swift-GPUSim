//
// Created by 徐向荣 on 2022/6/12.
//

#ifndef CACHE_H
#define CACHE_H

#include <string>
#include <map>
#include <bitset>
#include <list>
#include <utility>
#include <vector>
#include <queue>
#include <climits>
#include <iostream>
#include "mem_fetch.h"
#include "config_reader.h"
#include "delayqueue.h"
#include "dram.h"



#define MAX_DEFAULT_CACHE_SIZE_MULTIBLIER 4
#define MAX_WARP_PER_SM  1 << 6
#define SECTOR_CHUNCK_SIZE  4
#define SECTOR_SIZE  32

enum dram_ctrl_t { DRAM_FIFO = 0, DRAM_FRFCFS = 1 };

class gpu;
class memory_partition;
unsigned int LOGB2(unsigned int v);

enum set_index_function {
    LINEAR_SET_FUNCTION = 0,
    BITWISE_XORING_FUNCTION,
    HASH_IPOLY_FUNCTION
};

enum cache_block_state {
    INVALID = 0, RESERVED, VALID, MODIFIED
};

enum cache_request_status {
    HIT = 0,
    HIT_RESERVED,
    MISS,
    RESERVATION_FAIL,
    SECTOR_MISS,
    NUM_CACHE_REQUEST_STATUS
};

struct evicted_block_info {
    new_addr_type m_block_addr;
    unsigned m_modified_size;

    evicted_block_info() {
        m_block_addr = 0;
        m_modified_size = 0;
    }

    void set_info(new_addr_type block_addr, unsigned modified_size) {
        m_block_addr = block_addr;
        m_modified_size = modified_size;
    }
};

class mshr_table {
public:
    mshr_table(unsigned num_entries, unsigned max_merged)
            : m_num_entries(num_entries),
              m_max_merged(max_merged) {
    }

    bool probe(new_addr_type block_addr) const;

    bool full(new_addr_type block_addr) const;

    void add(new_addr_type block_addr, mem_fetch *mf);

    mem_fetch *next_access(new_addr_type address);


private:
    const unsigned m_num_entries;
    const unsigned m_max_merged;

    struct mshr_entry {
        std::list<mem_fetch *> m_list;
        bool m_has_atomic;

        mshr_entry() : m_has_atomic(false) {}
    };

    typedef std::map<new_addr_type, mshr_entry> table;
    table m_data;
    std::list<new_addr_type> m_current_response;
};

class cache_config {
public:
    cache_config(bool secter_cache, int n_banks, int n_sets, int line_size, int assoc,
                 const std::string &replacement_policy,
                 const std::string &write_policy, const std::string &alloc_policy,
                 const std::string &write_alloc_policy,
                 const std::string &set_index_function, const std::string &mshr_type, int mshr_entries,
                 int mshr_max_merge,
                 bool is_streaming) {
        m_n_banks = n_banks;
        m_n_sets = n_sets;
        m_line_size = line_size;
        m_assoc = assoc;
        m_replacement_policy = replacement_policy;
        m_write_policy = write_policy;
        m_alloc_policy = alloc_policy;
        m_write_alloc_policy = write_alloc_policy;
        if (set_index_function == "LINEAR_SET_FUNCTION") {
            m_set_index_function = LINEAR_SET_FUNCTION;
        } else if (set_index_function == "HASH_IPOLY_FUNCTION") {
            m_set_index_function = HASH_IPOLY_FUNCTION;
        } else {
            abort();
        }
        m_mshr_type = mshr_type;
        m_mshr_entries = mshr_entries;
        m_mshr_max_merge = mshr_max_merge;
        m_is_streaming = is_streaming;
        m_sector_cache = secter_cache;
        if (m_alloc_policy == "STREAMING") {
            m_alloc_policy = "ON FILL";
            m_mshr_entries = n_sets * m_assoc * MAX_DEFAULT_CACHE_SIZE_MULTIBLIER * SECTOR_CHUNCK_SIZE;
            m_mshr_max_merge = MAX_WARP_PER_SM;
        }
        m_atom_size = m_sector_cache ? SECTOR_SIZE : m_line_size;
    }

    new_addr_type tag(new_addr_type addr) const {
        return addr & ~(new_addr_type) (m_line_size - 1);
    }

    new_addr_type block_addr(new_addr_type addr) const {
        return addr & ~(new_addr_type) (m_line_size - 1);
    }

    new_addr_type mshr_addr(new_addr_type addr) const {
        return addr & ~(new_addr_type) (m_atom_size - 1);
    }

    unsigned set_index(new_addr_type addr) const {
        if(m_set_index_function == LINEAR_SET_FUNCTION){
            return hash_function(addr, m_n_sets, LOGB2(m_line_size), LOGB2(m_n_sets)); //linear
        }
        else if(m_set_index_function == HASH_IPOLY_FUNCTION){
            new_addr_type higher_bits = addr >> (LOGB2(m_line_size) + LOGB2(m_n_sets));
            unsigned index = (addr >> LOGB2(m_line_size)) & (m_n_sets - 1);
            std::bitset<64> a(higher_bits);
            std::bitset<6> b(index);
            std::bitset<6> new_index(index);

            new_index[0] = a[18] ^ a[17] ^ a[16] ^ a[15] ^ a[12] ^ a[10] ^ a[6] ^ a[5] ^
                           a[0] ^ b[0];
            new_index[1] = a[15] ^ a[13] ^ a[12] ^ a[11] ^ a[10] ^ a[7] ^ a[5] ^ a[1] ^
                           a[0] ^ b[1];
            new_index[2] = a[16] ^ a[14] ^ a[13] ^ a[12] ^ a[11] ^ a[8] ^ a[6] ^ a[2] ^
                           a[1] ^ b[2];
            new_index[3] = a[17] ^ a[15] ^ a[14] ^ a[13] ^ a[12] ^ a[9] ^ a[7] ^ a[3] ^
                           a[2] ^ b[3];
            new_index[4] = a[18] ^ a[16] ^ a[15] ^ a[14] ^ a[13] ^ a[10] ^ a[8] ^ a[4] ^
                           a[3] ^ b[4];
            new_index[5] =
                    a[17] ^ a[16] ^ a[15] ^ a[14] ^ a[11] ^ a[9] ^ a[5] ^ a[4] ^ b[5];
            return new_index.to_ulong();
        }
        else{

        }
    }


    unsigned hash_function(new_addr_type addr, unsigned m_nset,
                           unsigned m_line_sz_log2,
                           unsigned m_n_set_log2) const;


    unsigned get_max_num_lines() const {
        return m_n_sets * m_assoc;
    }

    bool m_sector_cache;
    int m_n_banks;
    int m_n_sets;
    int m_line_size;
    int m_assoc;
    std::string m_replacement_policy;
    std::string m_write_policy;
    std::string m_alloc_policy;
    std::string m_write_alloc_policy;
    int m_set_index_function;
    std::string m_mshr_type;
    int m_mshr_entries;
    int m_mshr_max_merge;
    bool m_is_streaming;
    int m_atom_size;

};

class memory_config{
public:
    void init(gpu_config &m_gpu_config){
        nbk = m_gpu_config.memory_config["nbk"];
        nbkgrp = m_gpu_config.memory_config["nbkgrp"];
        switch (m_gpu_config.memory_config["scheduler_type"]){
            case 0:
                scheduler_type = DRAM_FIFO;
                break;
            case 1:
                scheduler_type = DRAM_FRFCFS;
            default:
                printf("error config: memory config scheduler_type");
                exit(1);
                break;
        }
        gpgpu_dram_return_queue_size = m_gpu_config.memory_config["gpgpu_dram_return_queue_size"];
        gpgpu_frfcfs_dram_sched_queue_size = m_gpu_config.memory_config["gpgpu_frfcfs_dram_sched_queue_size"];
        m_n_sub_partition_per_memory_channel = m_gpu_config.memory_config["m_n_sub_partition_per_memory_channel"];
        seperate_write_queue_enabled = m_gpu_config.memory_config["seperate_write_queue_enabled"];
        gpgpu_frfcfs_dram_write_queue_size = m_gpu_config.memory_config["gpgpu_frfcfs_dram_write_queue_size"];
        dram_bnk_indexing_policy = m_gpu_config.memory_config["dram_bnk_indexing_policy"];
        dual_bus_interface = m_gpu_config.memory_config["dual_bus_interface"];
        tCCDL = m_gpu_config.memory_config["tCCDL"];
        tRRD = m_gpu_config.memory_config["tRRD"];
        tRCD = m_gpu_config.memory_config["tRCD"];
        tRAS = m_gpu_config.memory_config["tRAS"];
        tRP = m_gpu_config.memory_config["tRP"];
        tRC = m_gpu_config.memory_config["tRC"];
        CL = m_gpu_config.memory_config["CL"];
        WL = m_gpu_config.memory_config["WL"];
        tCDLR = m_gpu_config.memory_config["tCDLR"];
        tWR = m_gpu_config.memory_config["tWR"];
        nbkgrp = m_gpu_config.memory_config["nbkgrp"];
        tRTPL = m_gpu_config.memory_config["tRTPL"];
        data_command_freq_ratio = m_gpu_config.memory_config["data_command_freq_ratio"];
        dram_bnkgrp_indexing_policy = m_gpu_config.memory_config["dram_bnkgrp_indexing_policy"];
        tRCDWR = tRCD - (WL + 1);
        tCCD = m_gpu_config.memory_config["tCCD"];
        BL = m_gpu_config.memory_config["BL"];
         if (elimnate_rw_turnaround) {
            tRTW = 0;
            tWTR = 0;
        } else {
            tRTW = (CL + (BL / data_command_freq_ratio) + 2 - WL);
            tWTR = (WL + (BL / data_command_freq_ratio) + tCDLR);
        }
        tWTP = (WL + (BL / data_command_freq_ratio) + tWR);
        simple_dram_model = 0;
    }

    dram_ctrl_t scheduler_type;
    int gpgpu_dram_return_queue_size;

    int m_n_sub_partition_per_memory_channel;
    int gpgpu_frfcfs_dram_sched_queue_size;
    bool seperate_write_queue_enabled;
    int gpgpu_frfcfs_dram_write_queue_size;
    int dram_bnk_indexing_policy;
    int dram_atom_size;
    int dual_bus_interface;


    unsigned tCCDL;  // column to column delay when bank groups are enabled
    unsigned tRTPL;  // read to precharge delay when bank groups are enabled for

    unsigned tCCD;    // column to column delay
    unsigned tRRD;    // minimal time required between activation of rows in
    unsigned tRCD;    // row to column delay - time required to activate a row
    unsigned tRCDWR;  // row to column delay for a write command
    unsigned tRAS;    // time needed to activate row
    unsigned tRP;     // row precharge ie. deactivate row
    unsigned
            tRC;  // row cycle time ie. precharge current, then activate different row
    unsigned tCDLR;  
    unsigned tWR;    // Last data-in to Row precharge

    unsigned CL;    // CAS latency Column Address Strobe or Signal
    unsigned WL;    // WRITE latency
    unsigned BL;    // Burst Length in bytes (4 in GDDR3, 8 in GDDR5)
    unsigned tRTW;  // time to switch from read to write
    unsigned tWTR;  // time to switch from write to read
    unsigned tWTP;  // time to switch from write to precharge in the same bank
    unsigned busW;

    unsigned nbkgrp;  // number of bank groups (has to be power of 2)
    unsigned nbk;
    unsigned
            data_command_freq_ratio;  // frequency ratio between DRAM data bus and
    unsigned dram_bnkgrp_indexing_policy;
    bool elimnate_rw_turnaround = 0;
    bool simple_dram_model;

};

class sector_cache_block { //cache line
public:
    sector_cache_block() {
        m_tag = 0;
        m_block_addr = 0;
        init();
    }

    void init() {
        for (unsigned i = 0; i < SECTOR_CHUNCK_SIZE; ++i) {
            m_sector_alloc_time[i] = 0; 
            m_sector_fill_time[i] = 0; 
            m_last_sector_access_time[i] = 0;
            m_status[i] = INVALID;
            m_readable[i] = true;
        }
        m_line_alloc_time = 0;
        m_line_last_access_time = 0;
        m_line_fill_time = 0;
    }

    void allocate(new_addr_type tag, new_addr_type block_addr,
                  unsigned time, unsigned sector_mask) {
        allocate_line(tag, block_addr, time, sector_mask);
    }

    void allocate_line(new_addr_type tag, new_addr_type block_addr, unsigned time,
                       unsigned sector_mask) {
        init();
        m_tag = tag;
        m_block_addr = block_addr;
        m_sector_alloc_time[sector_mask] = time;
        m_last_sector_access_time[sector_mask] = time;
        m_sector_fill_time[sector_mask] = 0;
        m_status[sector_mask] = RESERVED;
        m_line_alloc_time = time;  
        m_line_last_access_time = time;
        m_line_fill_time = 0;
    }

    void allocate_sector(unsigned time, unsigned sector_mask) {
        m_sector_alloc_time[sector_mask] = time;
        m_last_sector_access_time[sector_mask] = time;
        m_sector_fill_time[sector_mask] = 0;
        m_status[sector_mask] = RESERVED;
        m_readable[sector_mask] = true;
        m_line_last_access_time = time;
        m_line_fill_time = 0;
    }

    void fill(unsigned time, unsigned sector_mask) {
        m_status[sector_mask] = VALID;
        m_sector_fill_time[sector_mask] = time;
        m_line_fill_time = time;
    }

    bool is_invalid_line() {
        // all the sectors should be invalid
        for (auto &m_statu: m_status) {
            if (m_statu != INVALID) return false;
        }
        return true;
    }

    bool is_valid_line() { return !(is_invalid_line()); }

    bool is_reserved_line() {
        for (auto &m_statu: m_status) {
            if (m_statu == RESERVED) return true;
        }
        return false;
    }

    bool is_modified_line() {
        for (auto &m_statu: m_status) {
            if (m_statu == MODIFIED) return true;
        }
        return false;
    }

    enum cache_block_state get_status(
            unsigned sector_mask) {
        return m_status[sector_mask];
    }

    void set_status(enum cache_block_state status,
                    unsigned sector_mask) {
        m_status[sector_mask] = status;
    }

    unsigned long long get_last_access_time() const {
        return m_line_last_access_time;
    }

    void set_last_access_time(unsigned long long time,
                              unsigned sector_mask) {
        m_last_sector_access_time[sector_mask] = time;
        m_line_last_access_time = time;
    }

    unsigned long long get_alloc_time() const { return m_line_alloc_time; }


    void set_modified_on_fill(bool m_modified,
                              unsigned sector_mask) {
        m_set_modified_on_fill[sector_mask] = m_modified;
    }

    void set_m_readable(bool readable,
                        unsigned sector_mask) {
        m_readable[sector_mask] = readable;
    }

    bool is_readable(unsigned sector_mask) {
        return m_readable[sector_mask];
    }

    new_addr_type m_tag;
    new_addr_type m_block_addr;
    unsigned m_sector_alloc_time[SECTOR_CHUNCK_SIZE];
    unsigned m_last_sector_access_time[SECTOR_CHUNCK_SIZE];
    unsigned m_sector_fill_time[SECTOR_CHUNCK_SIZE];
    unsigned m_line_alloc_time;
    unsigned m_line_last_access_time;
    unsigned m_line_fill_time;
    cache_block_state m_status[SECTOR_CHUNCK_SIZE];
    bool m_set_modified_on_fill[SECTOR_CHUNCK_SIZE];
    bool m_readable[SECTOR_CHUNCK_SIZE];

};

class tag_array {
public:
    tag_array(const cache_config &config, unsigned core_id) : m_config(config) {
        unsigned cache_lines_num = config.get_max_num_lines();
        m_lines = new sector_cache_block *[cache_lines_num];
        for (unsigned i = 0; i < cache_lines_num; ++i)
            m_lines[i] = new sector_cache_block();
        m_core_id = core_id; //sm_id
    }

    ~tag_array() {
        unsigned cache_lines_num = m_config.get_max_num_lines();
        for (unsigned i = 0; i < cache_lines_num; ++i) delete m_lines[i];
        delete[] m_lines;
    };

    unsigned
    tag_array_probe(new_addr_type addr, unsigned sector_mask, mem_fetch *mem_fetch, unsigned &idx);

    void tag_array_probe_idle(new_addr_type addr, unsigned int sector_mask, mem_fetch *mem_fetch,
                              int &idx);

    unsigned tag_array_access(new_addr_type addr, unsigned time, mem_fetch *mem_fetch, unsigned &idx);

    unsigned tag_array_access(new_addr_type addr, unsigned time, mem_fetch *mem_fetch, unsigned &idx, bool &wb);

    void tag_array_fill(new_addr_type addr, unsigned time, unsigned mask);

    sector_cache_block *get_block(unsigned idx) const { return m_lines[idx]; }

    cache_config m_config;
    sector_cache_block **m_lines; 

    unsigned m_core_id;
    int m_type_id;
    typedef std::map<new_addr_type, unsigned> line_table;
    line_table pending_lines;
};

class data_cache {
public:
    data_cache(const cache_config& cache_config, gpu_config &gpu_config, unsigned sm_id,gpu* gpu) :
            m_cache_config(cache_config),
            m_gpu_config(gpu_config),
            sm_id(sm_id){
        m_gpu = gpu;
        m_tag_array = new tag_array(cache_config, sm_id);
        m_mshrs = new mshr_table(cache_config.m_mshr_entries, cache_config.m_mshr_max_merge);
    }

    cache_config m_cache_config;
    gpu_config m_gpu_config;
    unsigned sm_id;
    tag_array *m_tag_array;
    mshr_table *m_mshrs;
    gpu* m_gpu;

    int m_write_cache_access_num = 0;
    int m_read_cache_access_num = 0;
    int m_load_sector_hit = 0;
    int m_store_sector_hit = 0;
    int m_load_sector_miss = 0;
    int m_store_sector_miss = 0;
    int m_sector_load_to_next = 0;
    int m_sector_store_to_next = 0;
};

class l1_data_cache : public data_cache {
public:
    l1_data_cache(cache_config &cache_config, gpu_config &gpu_config, unsigned sm_id,
                  std::map<int, std::queue<mem_fetch *>> &sm_to_l1, gpu* gpu) :
            data_cache(cache_config, gpu_config, sm_id,gpu), m_sm_to_l1(sm_to_l1) {
        m_tlb_hit = false;
        m_l1_to_l2_num = 0;
        m_l1_to_l2_mshr = 0;
    }

    bool m_tlb_hit;
    std::map<int, std::queue<mem_fetch *>> &m_sm_to_l1; 
    std::queue<mem_fetch *> m_miss_queue;
    std::queue<mem_fetch* > l1_to_sm; 
    int m_l1_to_l2_num;
    int m_l1_to_l2_mshr;


    void l1_cache_cycle(int cycles);

    void m_miss_queue_cycle(int cycles);

    unsigned cache_access(new_addr_type addr, mem_fetch *mf, unsigned time);

    void cache_fill(mem_fetch *mf, unsigned time);

};

class l2_data_cache : public data_cache {
public:
    l2_data_cache(cache_config &cache_config, gpu_config &gpu_config, int m_partition_id, gpu *gpu) :
            data_cache(cache_config, gpu_config, 999, gpu) { 
        m_device_id = std::stoi(gpu_config.m_gpu_config["sm_num"]) + m_partition_id;
        m_L2_dram_queue = new fifo_pipeline<mem_fetch>("L2-to-dram", 0, 999999);
        m_dram_L2_queue = new fifo_pipeline<mem_fetch>("dram-to-L2", 0, 999999);
        for (int i = 0; i < 3; i++) {
            std::queue<mem_fetch *> l2_to_sm_t;
            l2_to_sms.push_back(l2_to_sm_t);
        }
    }

    ~l2_data_cache(){
        delete m_L2_dram_queue;
        delete m_dram_L2_queue;
    }

    void init(memory_partition* mem){
        m_mem_partition = mem;
    }

    void l2_cache_cycle(int cycles);

    void send_l2_to_sm(mem_fetch *mf, int gpu_cycles, int latency) {
        mf->m_l2_ready_time = gpu_cycles + latency;
        if(latency==std::stoi(m_gpu_config.m_gpu_config["l2_hit_latency"])){
            l2_to_sms[0].push(mf);
        }else if(latency==std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"])){
            l2_to_sms[1].push(mf);
        }else if(latency==std::stoi(m_gpu_config.m_gpu_config["tlb_miss_latency"])){
            l2_to_sms[2].push(mf);
        }else{
            ;
        }
    }

    void send_l2_to_dram(mem_fetch *mf, int latency) const {
            m_L2_dram_queue->push(mf);
    }


    unsigned cache_access(new_addr_type addr, mem_fetch *mf, unsigned time);

    void cache_fill(mem_fetch *mf, unsigned time);

    unsigned get_id() { return m_device_id - std::stoi(m_gpu_config.m_gpu_config["sm_num"]); } // todo replace with sm_num

    static bool Compare(mem_fetch *a, mem_fetch *b) {
        return a->m_l2_ready_time > b->m_l2_ready_time;
    }

    int m_device_id; 
    std::queue<mem_fetch *> m_l1_to_l2; // l1 to l2
    std::vector<std::pair<mem_fetch *, int>> l2_to_sm;
    std::vector<std::queue<mem_fetch *>> l2_to_sms;

    std::vector<std::pair<mem_fetch *, int>> l2_to_dram;
    memory_partition* m_mem_partition;
    fifo_pipeline<mem_fetch> *m_L2_dram_queue;
    fifo_pipeline<mem_fetch> *m_dram_L2_queue;
    bool L2_dram_queue_empty() const;
    class mem_fetch *L2_dram_queue_top() const;
    bool dram_L2_queue_full() const;
    void dram_L2_queue_push(class mem_fetch *mf);
    void L2_dram_queue_pop();

};

class memory_partition  {
public:
    memory_partition(const cache_config &cache_config, const gpu_config &gpu_config, const memory_config *config, const gpu* gpu);

    ~memory_partition(){
        delete m_dram;
        for(int i = 0; i< std::stoi(m_gpu_config.m_gpu_config["mem_num"]); i++) {
            delete m_l2_caches[i];
        }
    }

    void print_stats();

    void memory_partition_cycle(int cycles);
    const gpu* get_mgpu() const{return m_gpu;}

    const memory_config *m_config;
    cache_config m_cache_config;
    gpu_config m_gpu_config;
    std::vector<l2_data_cache *> m_l2_caches;
    const gpu* m_gpu;
    void init(gpu* gpu);


    void dram_model_cycle(int);
    void dram_cycle(int);
    dram_t **m_dram;
    bool can_issue_to_dram(int inner_sub_partition_id);
    struct dram_delay_t {
        int ready_cycle;
        class mem_fetch *req;
        int l2_id;

    };
    std::list<dram_delay_t> *m_dram_latency_queue;

    int  *last_issued_partition;
};


#endif 
