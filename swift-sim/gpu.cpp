#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <utility>
#include <iomanip>

#include "gpu.h"

int print_latency = 0;


double delta(int hard, int mine) {
    if (hard > mine) { return 1.0 - 1.0 * mine / hard; }
    else if (hard < mine) { return 1.0 * mine / hard - 1; }
    else { return 0; }
}

void gpu::gpu_cycle(void *thread_arg) {
    int kernel_id = ((struct thread_data *) thread_arg)->kernel_id;
    std::string benchmark = ((struct thread_data *) thread_arg)->benchmark;
    bool sm_active = true;
    while (sm_active) {
        int total_l1_to_l2_num = 0;
        int total_l1_to_l2_mshr = 0;
        sm_active = false;
        gpu_sim_cycles += 1;
       
        // printf("gpu_sim_cycles:%d\n", gpu_sim_cycles);

        #if ANALYSIS == 0
        m_memory_partition->memory_partition_cycle(gpu_sim_cycles);
        m_icnt->Advance(); //icnt cycle
        #endif

        for (auto &sm: m_sm) {
            sm->sm_cycle();
            total_l1_to_l2_num += sm->m_l1_data_cache->m_l1_to_l2_num;
            total_l1_to_l2_mshr += sm->m_l1_data_cache->m_l1_to_l2_mshr;
            if (sm->sm_active()) {
                sm_active = true;// there is active sm
            }
        }
    }
    int stg_cycles = 0;
    #if ANALYSIS == 0
    int l2_rd_sector = 0, l2_wr_sector = 0, l2_rd_hit = 0, l2_wr_hit = 0;
    for (auto &l2_cache: m_memory_partition->m_l2_caches) {
        if (!l2_cache->l2_to_dram.empty()) {
            for (auto &remaining: l2_cache->l2_to_dram) {
                if (stg_cycles < remaining.second) {
                    stg_cycles = remaining.second;
                }
            }
        }

        l2_rd_sector += l2_cache->m_read_cache_access_num;
        l2_wr_sector += l2_cache->m_write_cache_access_num;
        l2_rd_hit += l2_cache->m_load_sector_hit;
        l2_wr_hit += l2_cache->m_store_sector_hit;
    }
    #endif
    gpu_sim_cycles += stg_cycles;
    // printf("gpu_sim_cycle:%d\n", gpu_sim_cycles);

    int total_sm_cycles = 0, l1_rd_sector = 0, l1_rd_hit = 0, l1_wr_sector = 0, l1_wr_hit = 0;
    int l1_rd_miss = 0;
    int l1_wr_miss = 0;
    int other_ld_hit = 0;
    int other_wr_hit = 0;
    long long all_inst = 0;
    long long total_thread_ldst_inst = 0;
    long long total_ldst_inst = 0; 
    long long total_inst = 0;
    for (auto &sm: m_sm) {
        if (sm->cycles > 0) {
            total_sm_cycles += sm->cycles + stg_cycles;
            total_sm_cycles += stoi(m_gpu_configs.m_gpu_config["kernel_launch_cycles"]);
            total_inst += sm->m_execute_inst;
            all_inst+=sm->m_total_inst;
            total_ldst_inst+=sm->m_ldst_inst;
            total_thread_ldst_inst += sm->m_execute_ldst_inst;
        }
        l1_rd_sector += sm->m_l1_data_cache->m_read_cache_access_num;
        l1_rd_hit += sm->m_l1_data_cache->m_load_sector_hit;
        l1_wr_sector += sm->m_l1_data_cache->m_write_cache_access_num;
        l1_wr_hit += sm->m_l1_data_cache->m_store_sector_hit;
        l1_rd_miss += sm->m_l1_data_cache->m_load_sector_miss;
        l1_wr_miss += sm->m_l1_data_cache->m_store_sector_miss;
    }
    total_sm_cycles +=
            stoi(m_gpu_configs.m_gpu_config["block_launch_cycles"]) * m_active_kernel->m_kernel_info.m_grid_size;

   printf("\n========= kernel %d =========\n", m_active_kernel->m_kernel_id);

    
   printf("total_sm_cycles:%d\n", total_sm_cycles);
   printf("total_thread_inst:%lld\n", total_inst);
   printf("total_thread_ldst_inst:%lld\n", total_thread_ldst_inst);
   printf("total_warp_inst:%lld\n", all_inst);
   printf("total_ldst_warp_inst:%lld\n", total_ldst_inst);

   #if ANALYSIS == 0
   printf("========= L1 Cache stats =========\n");
   printf("tota_l1_access:%d\n", l1_rd_sector+l1_wr_sector);
   printf("total_l1_hit:%d\n", l1_rd_hit+l1_wr_hit);
   printf("l1_rd_sector:%d\n", l1_rd_sector);
   printf("l1_rd_hit:%d\n", l1_rd_hit);
   printf("l1_rd_miss:%d\n", l1_rd_miss);
   printf("l1_wr_sector:%d\n", l1_wr_sector);
   printf("l1_wr_hit:%d\n", l1_wr_hit);
   printf("l1_wr_miss:%d\n", l1_wr_miss);
   printf("other_rd_hit:%d\n", other_ld_hit);
   printf("other_wr_hit:%d\n", other_wr_hit);
   printf("l1 hit rate: %f\n",(float)(l1_wr_hit+l1_rd_hit)/(l1_wr_sector+l1_rd_sector));
   printf("inter-core locality: %f\n",(float)(other_ld_hit+other_wr_hit)/(l1_wr_miss+l1_rd_miss));

   printf("========= L2 Cache stats =========\n");
   printf("total_l2_access:%d\n", l2_rd_sector+l2_wr_sector);
   printf("total_l2_hit:%d\n", l2_rd_hit+l2_wr_hit);
   printf("l2_rd_sector:%d\n", l2_rd_sector);
   printf("l2_rd_hit:%d\n", l2_rd_hit);
   printf("l2_wr_sector:%d\n", l2_wr_sector);
   printf("l2_wr_hit:%d\n", l2_wr_hit);
   printf("l2 hit rate: %f\n",(float)(l2_wr_hit+l2_rd_hit)/(l2_wr_sector+l2_rd_sector));

   printf("========= Request stats =========\n");
   #endif
}


void streaming_multiprocessor::sm_cycle() {
    if (sm_active()) {
        cycles += 1;
    } else {
        return;
    }

    #if ANALYSIS == 0
    m_queue_sm_to_l1_busy.reset();

    auto *mf = (mem_fetch *) m_gpu->m_icnt->Pop(m_sm_id);

    write_back();
    write_back_request_l2(mf);
    
    m_l1_data_cache->l1_cache_cycle(cycles);
    #endif

    del_inactive_block();

    #if ANALYSIS == 0
    for (auto &subcore: m_subcores) {
        for (auto &unit: subcore) {
            if (unit.first == "LDS_units") {
                unit.second->unit_cycle(cycles, m_gpu);
            }
        }
    }
    
    #endif
  
    while (block_vec.size() < m_max_blocks && m_active_kernel->block_pointer < m_active_kernel->m_blocks.size()) {
        m_active_kernel->m_blocks[m_active_kernel->block_pointer]->load_mem_request("/mnt/sda/xu/gpu_workload_traces/" + m_gpu->active_benchmark + "/traces/",
        std::stoi(m_gpu->m_gpu_configs.l1_cache_config["l1_cache_line_size"]),m_active_kernel->m_blocks[m_active_kernel->block_pointer]->m_block_id);
        block_vec[m_active_kernel->m_blocks[m_active_kernel->block_pointer]->m_block_id] = m_active_kernel->m_blocks[m_active_kernel->block_pointer];
        m_active_kernel->block_pointer++;
    }

    int t = schedule_warps_in_subcores();

    m_execute_inst += t;
  
}

int streaming_multiprocessor::schedule_warps_in_subcores() {
    int inst_executed = 0;
    std::bitset<4> active (0);
    for (int sub_num = 0; sub_num < subcores; sub_num++) {
        for (auto &unit: m_gpu_config.m_sm_pipeline_units) {
            if (check_unit(sub_num, unit.first)) {
                active[sub_num] = true;
                break;
            }
        }
    }

    for (auto &block: block_vec) {
        if (active.count() <= 0) {
            break;
        }
        if (block.second->is_active(cycles)) {
            for (auto it = block.second->warp_vec.begin(); it != block.second->warp_vec.end(); ) {
                if (active.count() <= 0) {
                    break;
                }
                auto &warp = it->second;
                if (warp->warp_point >= warp->m_insts.size() && warp->m_pending_mem_request_num == 0 && warp->pending_inst ==0) {
                    warp->active = false;
                    it = get_block(warp->m_block_id)->warp_vec.erase(it);
                    continue;
                }
                else it++;

                bool flag3 = false;
                int sub_num_t = 0;

                if(!(warp->warp_point < warp->m_insts.size()))
                    continue;
               

                if(!check_dependency(*warp))
                    continue;


              
               
                for (int sub_num = 0; sub_num < subcores; sub_num++) {
                    if (active[sub_num]) {
                        bool sub_check = check_unit_in_subcore(*warp, sub_num);
                            if (sub_check) {
                                flag3 = true;
                                sub_num_t = sub_num;
                                break;
                            }
                        }
                }

                if(!flag3)
                    continue;
                int current_inst_executed = issue_inst(*warp, sub_num_t);
                inst_executed += current_inst_executed;
                active[sub_num_t] = false;
                break;

            }
        }
    }
    return inst_executed;
}

int streaming_multiprocessor::issue_inst(warp &warp, int sub_num) {
    inst inst;
    int inst_issued = 0;
    for (int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["num_inst_dispatch_units_per_warp"]); ++i) {
        inst = warp.m_insts[warp.warp_point];
        m_total_inst++;
        auto &unit = m_subcores[sub_num][std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode])];
        if (unit->ready_time <= cycles) {
            if (std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode])=="LDS_units") {
                m_ldst_inst++;
                m_execute_ldst_inst+=inst.m_active_thread_num;
                LdStInst lsi;
                lsi.process(m_gpu_config, unit, warp, cycles, inst.m_pc, inst.m_pc_index, inst.m_active_thread_num,
                            inst.m_opcode, m_sm_id, m_queue_sm_to_l1, m_l1_cache_config, m_active_kernel, m_gpu);
            } else {
                int latency = std::get<0>(m_gpu_config.gpu_isa_latency[inst.m_opcode]);
                unit->ready_time += 32 / unit->unit_width;
                warp.completions.push_back(cycles + latency);
            }
            warp.warp_point += 1;
            inst_issued+=inst.m_active_thread_num;
        }
    }
    return inst_issued;
}

void streaming_multiprocessor::write_back() {
    while (true) {
        if (!m_l1_data_cache->l1_to_sm.empty()) {
            mem_fetch *mf_next = m_l1_data_cache->l1_to_sm.front();
            if (mf_next->m_l1_ready_time <= cycles) {
                int completion_index = mf_next->m_completion_index;
                block *m_block = get_block(mf_next->m_src_block_id);
                warp *m_warp = m_block->get_warp(mf_next->m_src_warp_id);
                if (m_warp->pending_request_can_decrease(completion_index)) {
                    bool request_completion = m_warp->decrease_pending_request(completion_index);
                    if (request_completion) {
                        m_warp->completions[completion_index] = cycles;
                    }
                    m_warp->m_pending_mem_request_num--;
                }
                delete mf_next;
                m_l1_data_cache->l1_to_sm.pop();
            } else {
                return;
            }
        } else {
            return;
        }
    }
}

void streaming_multiprocessor::write_back_request_l2(mem_fetch *mf) {
    if (!mf) {
        return;
    }
    #if ANALYSIS == 0
    new_addr_type mshr_address = mf->m_sector_address;
    if (!mf->is_write()) {
        while (m_l1_data_cache->m_mshrs->probe(mshr_address)) {
            mem_fetch *mf_next = m_l1_data_cache->m_mshrs->next_access(mshr_address);
            int block_id_t = mf_next->m_src_block_id;
            int warp_id_t = mf_next->m_src_warp_id;
            int completion_index_t = mf_next->m_completion_index;
            block *m_block_t = get_block(block_id_t);
            warp *m_warp_t = m_block_t->get_warp(warp_id_t);
            if (m_warp_t->pending_request_can_decrease(completion_index_t)) {
                bool request_completion = m_warp_t->decrease_pending_request(completion_index_t);
                if (request_completion) {
                    m_warp_t->completions[completion_index_t] = cycles;
                }
                m_warp_t->m_pending_mem_request_num--;
            } else {
                printf("error!\n");
                exit(1);
            }
            m_l1_data_cache->cache_fill(mf, cycles);
        }
    } else { 
    #endif
        int block_id = mf->m_src_block_id;
        int warp_id = mf->m_src_warp_id;
        int completion_index = mf->m_completion_index;
        block *m_block = get_block(block_id);
        warp *m_warp = m_block->get_warp(warp_id);
        if (m_warp->pending_request_can_decrease(completion_index)) {
            bool request_completion = m_warp->decrease_pending_request(completion_index);
            if (request_completion) {
                m_warp->completions[completion_index] = cycles;
            }
            m_warp->m_pending_mem_request_num--;
        } else {
            printf("%s (%s: %d) ERROR:warp pending request cannot decrease!\n", __FILE__, __FUNCTION__, __LINE__);
            printf("sm id: %d block id:%d warp id:%d\n", m_sm_id, m_warp->m_block_id, m_warp->m_warp_id);
            exit(1);
        }
    }
    #if ANALYSIS == 0

}
    #endif


bool streaming_multiprocessor::check_unit_in_subcore(warp &warp, int sub_num) {
    inst inst;
    inst = warp.m_insts[warp.warp_point];
    return check_unit(sub_num, std::get<1>(m_gpu_config.gpu_isa_latency[inst.m_opcode]));
}


bool streaming_multiprocessor::check_unit(int sub_num, const string &unit_name) {
//    printf(":: %s\n", unit_name.c_str());
    auto &unit = m_subcores[sub_num][unit_name];
    if (unit->ready_time <= cycles) {
        return true;
    }
    return false;
}


bool streaming_multiprocessor::check_dependency(warp &warp) {
    inst inst;
    inst = warp.m_insts[warp.warp_point];

    for(int i = inst.m_dependency.size()-1;i>=0;i--){
         if (cycles < warp.completions[inst.m_dependency[i]])
            return false;
    }
    return true;
}


void streaming_multiprocessor::del_inactive_block() {
    auto it = block_vec.begin();
    for (; it != block_vec.end();) {
        if (!((*it).second->is_active(cycles))) {
            auto tmp = it;
            it = block_vec.erase(tmp);
        } else {
            it++;
        }
    }
}


void streaming_multiprocessor::load_all_subcore_units() {
    sm_unit *unit_out = NULL;
    for (int i = 0; i < subcores; i++) {
        std::map<std::string, sm_unit *> subcore;
        for (auto &unit: m_gpu_config.m_sm_pipeline_units) {
            sm_unit *unit_t = new sm_unit(unit.first);
            if (unit.second < subcores) {
//                printf("::%s\n", unit.first.c_str());
                if (unit_out == NULL) {
                    unit_t->set_sm(this);
                    unit_t->unit_width = unit.second;
                    unit_out = unit_t;
                }
                subcore[unit.first] = unit_out;
                continue;
            }
            unit_t->set_sm(this);
            unit_t->unit_width = unit.second / subcores;
            subcore[unit.first] = unit_t;
        }
        m_subcores.push_back(subcore);

    }
}

gpu::gpu(std::string benchmark) : gpu_sim_cycles(0), mf_id(0) {
    active_benchmark = std::move(benchmark);
    init_config();
    build_gpu();
}

void gpu::init_config() {
    m_gpu_configs = gpu_config();
    m_gpu_configs.read_m_config();
}

void gpu::build_gpu() {
    int sm_num = std::stoi(m_gpu_configs.m_gpu_config["sm_num"]);
    cache_config m_l1_cache_config = cache_config(true, std::stoi(m_gpu_configs.l1_cache_config["l1_cache_n_banks"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_n_sets"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_line_size"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_cache_m_assoc"]),
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_replacement_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_write_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_alloc_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_write_alloc_policy"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_m_set_index_function"],
                                                  m_gpu_configs.l1_cache_config["l1_cache_mshr_type"],
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_m_mshr_entries"]),
                                                  std::stoi(m_gpu_configs.l1_cache_config["l1_m_mshr_max_merge"]),
                                                  true);

    cache_config m_l2_cache_config = cache_config(true,
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_sub_partitions"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_n_sets"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_line_size"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_cache_m_assoc"]),
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_replacement_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_write_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_alloc_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_write_alloc_policy"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_m_set_index_function"],
                                                  m_gpu_configs.l2_cache_config["l2_cache_mshr_type"],
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_m_mshr_entries"]),
                                                  std::stoi(m_gpu_configs.l2_cache_config["l2_m_mshr_max_merge"]),
                                                  true);
    m_memory_config = new memory_config;
    m_memory_config->init(m_gpu_configs);

    #if ANALYSIS == 0
    m_memory_partition = new memory_partition(m_l2_cache_config, m_gpu_configs, m_memory_config, this);
    m_memory_partition->init(this);
    creat_icnt();
    #endif

    for (int sm_id = 0; sm_id < sm_num; sm_id++) {
        auto *sm = new streaming_multiprocessor(sm_id, &m_gpu_configs, m_l1_cache_config);
        sm->init(this);
        m_sm.push_back(sm);
    }

}

void gpu::read_trace(int kernel_id, kernel_info &kernelInfo,
                     std::vector<trace_inst> &trace_insts) {
    traceReader = new trace_reader(trace_reader(kernel_id));
    kernelInfo = kernel_info();

    traceReader->read_sass("/mnt/sda/xu/gpu_workload_traces/" + active_benchmark + "/traces/", kernelInfo,
                           trace_insts);
}


int gpu::max_block_per_sm(kernel_info kernel_info) {
    return std::min(std::min(max_block_limit_by_smem(kernel_info), max_block_limit_by_regs(kernel_info)),
                    std::min(max_block_limit_by_warps(kernel_info), max_block_limit_by_others()));
}

int gpu::max_block_limit_by_regs(kernel_info kernel_info) {
    int blocks_per_sm_limit_regs;
    int allocated_active_warps_per_block = (int) (
            ceil(((float) (kernel_info.m_block_size) / (float) m_gpu_configs.m_compute_capability["warp_size"]), 1));

    if (kernel_info.m_num_registers == 0) {
        blocks_per_sm_limit_regs = m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"];
    } else {
        int allocated_regs_per_warp = ceil(
                ((float) (kernel_info.m_num_registers) * (float) (m_gpu_configs.m_compute_capability["warp_size"])),
                (float) m_gpu_configs.m_compute_capability["register_allocation_size"]);
        int allocated_regs_per_sm = (int) (
                floor((float) (m_gpu_configs.m_compute_capability["max_registers_per_block"]) /
                      (float) allocated_regs_per_warp,
                      std::stof(m_gpu_configs.m_gpu_config["num_warp_schedulers_per_SM"])));
        blocks_per_sm_limit_regs = (int) (
                floor(((float) allocated_regs_per_sm / (float) allocated_active_warps_per_block), 1)
                * floor(((float) m_gpu_configs.m_compute_capability["max_registers_per_SM"] /
                         (float) m_gpu_configs.m_compute_capability["max_registers_per_block"]), 1));
    }
    return blocks_per_sm_limit_regs;
}

int gpu::max_block_limit_by_smem(kernel_info kernel_info) {
    int blocks_per_sm_limit_smem;
    if (kernel_info.m_shared_mem_bytes == 0) {
        blocks_per_sm_limit_smem = m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"];
    } else {
        float smem_per_block = ceil((float) kernel_info.m_shared_mem_bytes,
                                    (float) m_gpu_configs.m_compute_capability["smem_allocation_size"]);
        blocks_per_sm_limit_smem = (int) (floor(
                (std::stof(m_gpu_configs.m_gpu_config["shared_mem_size"]) / smem_per_block),
                1));
    }

    return blocks_per_sm_limit_smem;
}

int gpu::max_block_limit_by_warps(kernel_info kernel_info) {
    int allocated_active_warps_per_block = (int) (
            ceil(((float) (kernel_info.m_block_size) / (float) (m_gpu_configs.m_compute_capability["warp_size"])),
                 1));
    int blocks_per_sm_limit_warps = (int) (std::min(m_gpu_configs.m_compute_capability["max_active_blocks_per_SM"],
                                                    (int) (floor(
                                                            ((float) m_gpu_configs.m_compute_capability["max_active_threads_per_SM"] /
                                                             (float) m_gpu_configs.m_compute_capability["warp_size"] /
                                                             (float) allocated_active_warps_per_block), 1))));
    return blocks_per_sm_limit_warps;


}

int gpu::max_block_limit_by_others() {
    return INT_MAX;
}

void gpu::launch_kernel(int kernel_id) {
    #if ANALYSIS == 0
    m_active_kernel = new kernel(kernel_id);
    read_trace(kernel_id, m_active_kernel->m_kernel_info, m_active_kernel->trace_insts);
    #endif
    m_active_kernel->init_blocks();

    for (auto &sm: m_sm) {
        sm->m_active_kernel = m_active_kernel;
    }
}


void gpu::execute_kernel(int kernel_id) {
    first_spawn_block();
}


int ceil(float x, float s) {
    return (int) (s * std::ceil((float) x / s));
}


int floor(float x, float s) {
    return (int) (s * std::floor((float) x / s));
}


void gpu::first_spawn_block() {
    int max_blocks = max_block_per_sm(m_active_kernel->m_kernel_info);

    int active_sms = std::min((int) m_active_kernel->m_kernel_info.m_grid_size,
                              std::stoi(m_gpu_configs.m_gpu_config["sm_num"]));
    for (auto &sm: m_sm) {
        sm->m_max_blocks = max_blocks;
    }
    int index = 0;
    for (auto &block: m_active_kernel->m_blocks) {
        if (m_sm[index]->block_vec.size() < max_blocks) {

            block.second->load_mem_request("/mnt/sda/xu/gpu_workload_traces/" + active_benchmark + "/traces/",
                                           std::stoi(m_gpu_configs.l1_cache_config["l1_cache_line_size"]),
                                           block.second->m_block_id);
            m_sm[index]->block_vec[block.first] = block.second;
            m_sm[index]->is_active = true;
            m_active_kernel->block_pointer++;
            index++;
            index %= active_sms;
        }
    }
}

#if ANALYSIS == 0

void sm_unit::unit_cycle(int cycles, gpu *p_gpu) {
    if (unit_name != "LDS_units")
        return;
    if (m_inst == nullptr || m_warp == nullptr)
        return;
    if (mem_fetch_point >= m_inst->m_coalesced_address.size())
        return;
    auto address = m_inst->m_coalesced_address[mem_fetch_point];
    int m_bank_id = address.second;
    if (m_sm->m_queue_sm_to_l1_busy[m_bank_id]) 
        return;
    unsigned int sm_id = m_sm->m_sm_id;
    int m_access_type = (m_inst->m_opcode.find("LDG") != string::npos || m_inst->m_opcode.find("LD") != string::npos)
                        ? 0 : 1;

    unsigned long long m_address = address.first;
    unsigned m_sector_mask = address.second;
    unsigned m_space = GLOBAL;
    bool m_is_atomic = false;
    long long m_gen_time = cycles;
    int m_src_block_id = m_warp->m_block_id;
    int m_src_warp_id = m_warp->m_warp_id;
    int m_completion_index = m_inst->completions_index;
    auto *mf = new mem_fetch(sm_id, m_access_type, m_address, m_sector_mask, m_space,
                             m_is_atomic, m_gen_time, m_src_block_id, m_src_warp_id,
                             m_completion_index, p_gpu);
    mf->pc = m_inst->m_pc;
    mf->pc_index = m_inst->m_pc_index;
    mem_fetch_point++;
    int bank_id = (int) l1_bank_hash(mf);
    m_sm->m_queue_sm_to_l1[bank_id].push(mf);
    m_sm->m_queue_sm_to_l1_busy.set(bank_id);
    m_warp->increase_pending_request(m_completion_index);
    m_warp->m_pending_mem_request_num++;
    m_sm->m_active_kernel->request_nums++;
    if (mem_fetch_point == m_inst->m_coalesced_address.size()) {
        mem_fetch_point = -1;
        m_warp->pending_inst--;
        m_inst = nullptr;
        m_warp = nullptr;
        ready_time = cycles;
        unit_latency = 0;
    }
}

#endif

void
LdStInst::process(gpu_config &gpu_config_t, sm_unit *unit, warp &warp, int cycles, long long int pc_t, int pc_index_t,
                  int active_thread_num_t, const string &opcode, unsigned int sm_id_t,
                  std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t, cache_config &l1_cache_config_t,
                  kernel *active_kernel_t, gpu *p_gpu) {
    if (opcode.find("LDG") != string::npos || opcode.find("STG") != string::npos ||
        opcode.find("LD.") != string::npos || opcode.find("ST.") != string::npos) {
        process_LDG_STG(gpu_config_t, unit, warp, cycles, pc_t, pc_index_t, active_thread_num_t,
                        opcode, sm_id_t, queue_sm_to_l1_t, l1_cache_config_t, active_kernel_t);
    } else if (opcode.find("LDS") != string::npos || opcode.find("STS") != string::npos ||
               opcode.find("ATOMS") != string::npos) {
        process_LDS_STS_ATOMS(gpu_config_t, active_thread_num_t, unit, warp, cycles);
    } else if (opcode.find("ATOM") != string::npos || opcode.find("ATOMG") != string::npos) {
        process_ATOM_ATOMG(unit, warp, pc_t, pc_index_t, sm_id_t, cycles, queue_sm_to_l1_t, l1_cache_config_t, p_gpu);
    } else {
        int latency_t = std::get<0>(gpu_config_t.gpu_isa_latency[opcode]);
        unit->ready_time += 32 / unit->unit_width;
        warp.completions.push_back(cycles + latency_t);
    }
}

void LdStInst::process_LDS_STS_ATOMS(gpu_config &gpu_config_t, int active_thread_num_t, sm_unit *unit,
                                     warp &warp, int cycles) {
    int latency_t;
    if (active_thread_num_t < 2) {
        latency_t = 8;
    } else if (active_thread_num_t < 4) {
        latency_t = 10;
    } else if (active_thread_num_t < 8) {
        latency_t = 14;
    } else if (active_thread_num_t < 16) {
        latency_t = 22;
    } else if (active_thread_num_t < 32) {
        latency_t = 37;
    } else {
        latency_t = 69;
    }
    unit->ready_time += 32 / unit->unit_width;
    warp.completions.push_back(cycles + latency_t);
}

void LdStInst::process_ATOM_ATOMG(sm_unit *unit, warp &warp, long long int pc_t, int pc_index_t, unsigned int sm_id_t,
                                  int cycles, std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t,
                                  cache_config &l1_cache_config_t, gpu *p_gpu) {
    unit->ready_time += 4;
    #if ANALYSIS == 0
    warp.completions.push_back(INT_MAX);
    mem_inst *m_mem_inst = warp.get_mem_inst(pc_t, pc_index_t);
    for (auto &address: m_mem_inst->m_coalesced_address) {
        unsigned int sm_id = sm_id_t;
        int m_access_type = 1;  //read
        unsigned long long m_address = address.first;
        unsigned m_sector_mask = address.second;
        unsigned m_space = GLOBAL;
        bool m_is_atomic = true;
        long long m_gen_time = cycles;
        int m_src_block_id = warp.m_block_id;
        int m_src_warp_id = warp.m_warp_id;
        int m_completion_index = (int) warp.completions.size() - 1;
        auto *mf = new mem_fetch(sm_id, m_access_type, m_address, m_sector_mask, m_space,
                                 m_is_atomic, m_gen_time, m_src_block_id, m_src_warp_id,
                                 m_completion_index, p_gpu);
        queue_sm_to_l1_t[l1_bank_hash(mf)].push(mf);
        warp.increase_pending_request(m_completion_index);
        warp.m_pending_mem_request_num++;
    }
    #endif
}

void LdStInst::process_LDG_STG(gpu_config &gpu_config_t, sm_unit *unit, warp &warp, int cycles, long long int pc_t,
                               int pc_index_t, int active_thread_num_t, const string &opcode, unsigned int sm_id_t,
                               std::map<int, std::queue<mem_fetch *>> &queue_sm_to_l1_t,
                               cache_config &l1_cache_config_t, kernel *active_kernel_t) {
    mem_inst *m_mem_inst = warp.get_mem_inst(pc_t, pc_index_t);
    if (m_mem_inst == nullptr || active_thread_num_t == 0 || m_mem_inst->m_coalesced_address.empty()) {
        int latency = 1;
        unit->ready_time += 32 / unit->unit_width;
        warp.completions.push_back(cycles + latency);
    } else {
        #if ANALYSIS == 0
        warp.completions.push_back(INT_MAX);
        unit->ready_time = INT_MAX;
        m_mem_inst->completions_index = warp.completions.size() - 1;
        unit->set_ldst_inst(m_mem_inst, &warp);
        warp.pending_inst++;
        #endif
    }
}

