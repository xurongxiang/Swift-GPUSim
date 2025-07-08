#ifndef GPU_H
#define GPU_H

#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "config_reader.h"
#include "trace_reader.h"
#include "cache.h"
#include "mem_fetch.h"
#include "crossbar.h"



extern int request_id;
class streaming_multiprocessor;
class sm_unit;
class mem_fetch;

struct thread_data {
	std::string benchmark;
	int kernel_id;
};

class gpu {
public:
    explicit gpu(std::string benchmark);

    ~gpu() {
        delete traceReader;
        delete m_active_kernel;
        delete m_memory_partition;
        delete m_memory_config;
    }

    void init_config();

    void build_gpu();

    void creat_icnt(){
        g_icnt_config.in_buffer_limit = 512;
        g_icnt_config.out_buffer_limit = 512;
        g_icnt_config.subnets = 2;
        g_icnt_config.arbiter_algo = NAIVE_RR;
        m_icnt = LocalInterconnect::New(g_icnt_config);
        m_icnt->CreateInterconnect(stoi(m_gpu_configs.m_gpu_config["sm_num"]),stoi(m_gpu_configs.m_gpu_config["mem_num"])); //new X-bar
    }

    void launch_kernel(int kernel_id);

    void execute_kernel(int kernel_id);

    void read_trace(int kernel_id, kernel_info &kernelInfo,
                    std::vector<trace_inst> &trace_insts);

    void first_spawn_block();

    void gpu_cycle(void* thread_arg);


    gpu_config m_gpu_configs;
    struct inct_config g_icnt_config;
    LocalInterconnect* m_icnt;
    std::string active_benchmark;
    trace_reader* traceReader;
    kernel* m_active_kernel;
    std::vector<streaming_multiprocessor *> m_sm;
    memory_partition* m_memory_partition;
    std::map<int, std::queue<mem_fetch*>> m_queue_l1_to_l2; 
    std::vector<std::pair<mem_fetch*, int>> l2_to_sm; 
    unsigned get_mf_id() const{return mf_id;}
    void increase_mf_id(){mf_id++;}
    int get_gpu_sim_cycles() const{return gpu_sim_cycles;}
    std::unordered_map<new_addr_type, std::unordered_set<int>> address_to_sms;
    long long total_access = 0;
    long long l2_push_num = 0;
    long long l2_pop_num = 0;




private:
    int max_block_per_sm(kernel_info kernel_info);

    static int max_block_limit_by_others();

    int max_block_limit_by_warps(kernel_info kernelInfo);

    int max_block_limit_by_regs(kernel_info kernelInfo);

    int max_block_limit_by_smem(kernel_info kernelInfo);

    int gpu_sim_cycles;
    
    unsigned mf_id;

    memory_config *m_memory_config;
};


class streaming_multiprocessor {
public:
    streaming_multiprocessor(unsigned sm_id, gpu_config *gpu_config_t, cache_config &cache_config_t)
            : m_l1_cache_config(cache_config_t), m_gpu_config(*gpu_config_t), cycles(0){
        m_sm_id = sm_id;
        is_active = false;
        m_max_blocks = 0;
        m_execute_inst = 0;
        m_ldst_inst = 0;
        m_total_inst = 0;
        m_queue_sm_to_l1_busy.reset();
        load_all_subcore_units();
    }

    void init(gpu* gpu){
        m_gpu = gpu;
        m_l1_data_cache = new l1_data_cache(m_l1_cache_config, m_gpu_config, m_sm_id, m_queue_sm_to_l1,gpu);
    }

    int schedule_warps_in_subcores();

    bool check_unit_in_subcore(warp &warp, int sub_num);

    bool check_unit(int sub_num, const string &unit_name);

    bool check_dependency(warp &);


    int issue_inst(warp &, int sub_num);

    void sm_cycle();

    void load_all_subcore_units();

    bool sm_active() {
        for (auto &block: block_vec) {
            if (block.second->is_active(cycles)) {
                is_active = true;
                return is_active;
            }
        }
        if (m_active_kernel->block_pointer < m_active_kernel->m_blocks.size()) {
            is_active = true;
            return is_active;
        }
        is_active = false;
        return false;
    }

    void del_inactive_block();

    void init_sm_to_l1() {
        int n_banks = m_l1_cache_config.m_n_banks;
        for (int i = 0; i < n_banks; i++) {
            m_queue_sm_to_l1[i] = {};
        }
    }

    block *get_block(int block_id) {

        if(block_vec.find(block_id)!=block_vec.end()){
            return block_vec[block_id];
        }
        printf("ERROR %s:%d block:%d is not found!\n", __FILE__, __LINE__, block_id);
        exit(1);
        return nullptr;
    }

    void write_back();

    void write_back_request_l2(mem_fetch * mf);

    cache_config m_l1_cache_config;


    l1_data_cache *m_l1_data_cache;


    gpu* m_gpu;
    std::vector<std::map<std::string, sm_unit*> > m_subcores;
    int subcores = 4;
    unsigned m_sm_id;
    int m_max_blocks;
    std::map<int,block *> block_vec;
    bool is_active;
    gpu_config m_gpu_config;
    int cycles;
    kernel *m_active_kernel;
    int m_execute_inst;
    int m_execute_ldst_inst;
    int m_ldst_inst;
    int m_total_inst;
    std::map<int, std::queue<mem_fetch *>> m_queue_sm_to_l1; 
    std::bitset<4> m_queue_sm_to_l1_busy;


    std::vector<mem_fetch *> m_response_fifo;

};

class sm_unit{
public:
    sm_unit(std::string name){
        unit_name = std::move(name);
        ready_time = 0;
        mem_fetch_point = 0;
        m_inst = nullptr;
        m_warp = nullptr;
        unit_latency = 0;
    }

    void set_sm(streaming_multiprocessor* sm){
        m_sm = sm;
    }

    void set_ldst_inst(mem_inst* inst, warp* warp){
        m_inst = inst;
        m_warp = warp;
        ready_time = INT_MAX;
        mem_fetch_point = 0; 
        unit_latency = 4;
    }

    void unit_cycle(int cycles, gpu*);

    std::string unit_name;
    int ready_time;
    mem_inst* m_inst;
    warp* m_warp;
    int mem_fetch_point;
    streaming_multiprocessor* m_sm;
    int unit_latency;
    int unit_width;

};

class LdStInst{
public:
    void process(gpu_config& gpu_config_t, sm_unit* unit, warp& warp, int cycles, long long int pc_t,
                 int pc_index_t, int active_thread_num_t, const string& opcode, unsigned sm_id_t,
                 std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t, cache_config& l1_cache_config_t,
                 kernel* active_kernel_t, gpu*);
    void process_LDG_STG(gpu_config& gpu_config_t, sm_unit* unit, warp& warp, int cycles, long long int pc_t, int pc_index_t,
                         int active_thread_num_t, const string& opcode, unsigned sm_id_t, std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t, cache_config& l1_cache_config_t, kernel*active_kernel_t);
    static void process_LDS_STS_ATOMS(gpu_config& gpu_config_t, int active_thread_num_t, sm_unit* unit, warp& warp, int cycles);
    void process_ATOM_ATOMG(sm_unit* unit, warp& warp, long long pc_t, int pc_index_t, unsigned sm_id_t,
                            int cycles, std::map<int, std::queue<mem_fetch *>>& queue_sm_to_l1_t,
                            cache_config& l1_cache_config_t, gpu*);

};

int ceil(float x, float s);

int floor(float x, float s);

#endif 
