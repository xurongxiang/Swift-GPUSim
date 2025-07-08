//
// Created by 徐向荣 on 2022/6/12.
//
// 
// The cache module in Swift-Sim reuses some code from gpgpu-sim, such as MSHR and DRAM modeling.
// Accel-Sim : https://github.com/accel-sim/accel-sim-framework.git
// gpgpu-sim : https://github.com/gpgpu-sim/gpgpu-sim_distribution.git


#include "cache.h"
#include "gpu.h"

#if ANALYSIS == 0

unsigned int LOGB2(unsigned int v) {
    unsigned int shift;
    unsigned int r;

    r = 0;

    shift = ((v & 0xFFFF0000) != 0) << 4;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xFF00) != 0) << 3;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xF0) != 0) << 2;
    v >>= shift;
    r |= shift;
    shift = ((v & 0xC) != 0) << 1;
    v >>= shift;
    r |= shift;
    shift = ((v & 0x2) != 0) << 0;
    v >>= shift;
    r |= shift;

    return r;
}

unsigned cache_config::hash_function(new_addr_type addr, unsigned m_nset,
                                     unsigned m_line_sz_log2,
                                     unsigned m_n_set_log2) const {
    unsigned set_index = 0;
    set_index = (addr >> m_line_sz_log2) & (m_nset - 1); // >> 32
    return set_index;
}


bool mshr_table::probe(new_addr_type block_addr) const {
    auto a = m_data.find(block_addr);
    return a != m_data.end();
}

bool mshr_table::full(new_addr_type block_addr) const {
    auto i = m_data.find(block_addr);
    if (i != m_data.end())
        return i->second.m_list.size() >= m_max_merged; // 最大聚合数
    else
        return m_data.size() >= m_num_entries; // MSHR最大记录数
}


void mshr_table::add(new_addr_type block_addr, mem_fetch *mf) {
    m_data[block_addr].m_list.push_back(mf);
    if (mf->is_atomic()) {
        m_data[block_addr].m_has_atomic = true;
    }
}


mem_fetch *mshr_table::next_access(new_addr_type address) {
    //address only correct in sector_cache case
    mem_fetch *result = m_data[address].m_list.front();
    m_data[address].m_list.pop_front();
    if (m_data[address].m_list.empty()) {
        // release entry
        m_data.erase(address);
    }
    return result;
}

void
tag_array::tag_array_probe_idle(new_addr_type addr, unsigned int sector_mask, mem_fetch *mem_fetch,
                                int &idx) {
    unsigned set_index = m_config.set_index(addr);
    for (unsigned way = 0; way < m_config.m_assoc; way++) {
        unsigned index = set_index * m_config.m_assoc + way;
        sector_cache_block *line = m_lines[index];
        if (line->is_invalid_line()) {
            idx = (int)index;
            return;
        }
    }
    idx = -1;
}

unsigned
tag_array::tag_array_probe(new_addr_type addr, unsigned int sector_mask, mem_fetch *mem_fetch,
                           unsigned &idx) {

    unsigned set_index = m_config.set_index(addr);
    new_addr_type tag = m_config.tag(addr);

    auto invalid_line = (unsigned) -1;
    auto valid_line = (unsigned) -1;
    unsigned long long valid_timestamp = (unsigned) -1;
    bool all_reserved = true;

    for (unsigned way = 0; way < m_config.m_assoc; way++) {
        unsigned index = set_index * m_config.m_assoc + way;
        sector_cache_block *line = m_lines[index];
        if (line->m_tag == tag) {
            if (line->get_status(sector_mask) == RESERVED) {
                idx = index;
                return HIT_RESERVED;
            } else if (line->get_status(sector_mask) == VALID) {
                idx = index;
                return HIT;
            } else if (line->get_status(sector_mask) == MODIFIED) {
                if (line->is_readable(sector_mask)) {
                    idx = index;
                    return HIT;
                } else {
                    idx = index;
                    return SECTOR_MISS;
                }
            } else if (line->is_valid_line() && line->get_status(sector_mask) == INVALID) {
                idx = index;
                return SECTOR_MISS;
            } else { ;
            }
        }
        if (!line->is_reserved_line()) {
            all_reserved = false;
            if (line->is_invalid_line()) {
                invalid_line = index;
            } else {
                // valid line : keep track of most appropriate replacement candidate
                if (m_config.m_replacement_policy == "LRU") {
                    if (line->get_last_access_time() < valid_timestamp) {
                        valid_timestamp = line->get_last_access_time();
                        valid_line = index;
                    }
                } else if (m_config.m_replacement_policy == "FIFO") {
                    if (line->get_alloc_time() < valid_timestamp) {
                        valid_timestamp = line->get_alloc_time();
                        valid_line = index;
                    }
                }
            }
        }
    }
    if (all_reserved) {
        return RESERVATION_FAIL;  // miss and not enough space in cache to allocate
    }
    if (invalid_line != (unsigned) -1) {
        idx = invalid_line;
    } else if (valid_line != (unsigned) -1) {
        idx = valid_line;
    } else
        abort();  // if an unreserved block exists, it is either invalid or

    return MISS;
}


unsigned
tag_array::tag_array_access(new_addr_type addr, unsigned int time, mem_fetch *mem_fetch, unsigned int &idx, bool &wb) {
    unsigned status = tag_array_probe(addr, mem_fetch->m_sector_mask, mem_fetch, idx);
    switch (status) {
        case HIT_RESERVED:
        case HIT:
            m_lines[idx]->set_last_access_time(time, mem_fetch->m_sector_mask);
            break;
        case MISS:
            break;
        case SECTOR_MISS:
            break;
        case RESERVATION_FAIL:
            break;
        default:
            abort();
    }
    return status;
}

void tag_array::tag_array_fill(new_addr_type addr, unsigned int time, unsigned int mask) {
    unsigned idx;
    unsigned status = tag_array_probe(addr, mask, nullptr, idx);
    if (status == MISS)
        m_lines[idx]->allocate(m_config.tag(addr), m_config.block_addr(addr), time, mask);
    else if (status == SECTOR_MISS) {
        ((sector_cache_block *) m_lines[idx])->allocate_sector(time, mask);
    }
    m_lines[idx]->fill(time, mask);
}

unsigned tag_array::tag_array_access(new_addr_type addr, unsigned int time, mem_fetch *mem_fetch, unsigned int &idx) {
    bool wb = false;
    unsigned result = tag_array_access(addr, time, mem_fetch, idx, wb);
    return result;
}


unsigned l1_data_cache::cache_access(new_addr_type addr, mem_fetch *mf, unsigned int time) {
    bool wr = mf->is_write();
    bool wb = false;
    if (wr) {
        m_write_cache_access_num++;
    } else {
        m_read_cache_access_num++;
    }
    new_addr_type block_addr = m_cache_config.block_addr(addr);
    auto cache_index = (unsigned) -1; //if hit index miss next evict index

    unsigned probe_status = m_tag_array->tag_array_probe(addr, mf->m_sector_mask, mf, cache_index);
    if (wr) {
        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_store_sector_hit++;
            if (m_cache_config.m_write_policy == "LOCAL_WB_GLOBAL_WT") {
                if (mf->m_space == GLOBAL) {
                    sector_cache_block *block = m_tag_array->get_block(cache_index);
                    mf->m_write_type = WRITE_THROUGH;
                    auto *mf_write_through = new mem_fetch(sm_id, mf->m_access_type, mf->m_address, mf->m_sector_mask, mf->m_space,
                             mf->m_is_atomic, mf->m_gen_time, mf->m_src_block_id, mf->m_src_warp_id,
                             mf->m_completion_index, m_gpu);
                   mf_write_through->m_write_type = WRITE_THROUGH_L2;
                   m_miss_queue.push(mf_write_through);
                   block->set_status(MODIFIED, mf->m_sector_mask);
                } else if (mf->m_space == LOCAL) {
                    m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                    sector_cache_block *block = m_tag_array->get_block(cache_index);
                    block->set_status(MODIFIED, mf->m_sector_mask);
                }
            } else if (m_cache_config.m_write_policy == "WRITE_BACK") {
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                sector_cache_block *block = m_tag_array->get_block(cache_index);
                block->set_status(MODIFIED, mf->m_sector_mask);
            } else {
                printf("error");
            }
            mf->m_status = L1_HIT;
            if(m_gpu_config.m_gpu_config.find("l1_hit_latency") == m_gpu_config.m_gpu_config.end()){
                printf("l1_hit_latency does not exist!\n");
                exit(1);
            }
            mf->m_l1_ready_time = std::stoi(m_gpu_config.m_gpu_config["l1_hit_latency"])+(int)time;
            l1_to_sm.push(mf);
            return HIT;
        } else {
            // m_gpu->address_to_sms[addr].insert(sm_id);
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
            bool mshr_hit = m_mshrs->probe(mshr_addr);
            if (mshr_hit) {
                 m_store_sector_hit++;
            }else{
                 m_store_sector_miss++;
                 //To get locality
                 new_addr_type block_addr = m_cache_config.block_addr(addr);
                 auto cache_index = (unsigned) -1; 
            }
            mf->m_tlb_hit = m_tlb_hit;
            if(!m_tlb_hit){
                m_tlb_hit = true;
            }
            m_miss_queue.push(mf);
            m_sector_store_to_next++;
            return MISS;
        }
    } else {
        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            // m_gpu->address_to_sms[addr].insert(sm_id);
            m_load_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
            mf->m_status = L1_HIT;
            mf->m_l1_ready_time = std::stoi(m_gpu_config.m_gpu_config["l1_hit_latency"])+(int)time;
            l1_to_sm.push(mf);
            return HIT;
        } else {
            // m_gpu->address_to_sms[addr].insert(sm_id);
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
            bool mshr_hit = m_mshrs->probe(mshr_addr);
            bool mshr_avail = !m_mshrs->full(mshr_addr);
            if (!mshr_avail) {
                return RESERVATION_FAIL;
            }
            if (mshr_hit) {
                m_load_sector_hit++;
                m_mshrs->add(mshr_addr, mf);
                m_l1_to_l2_mshr++;
            } else {
                m_load_sector_miss++;
                //To get Locality
                new_addr_type block_addr = m_cache_config.block_addr(addr);
                auto cache_index = (unsigned) -1; //if hit index miss next evict index
                m_mshrs->add(mshr_addr, mf);
                mf->m_tlb_hit = m_tlb_hit;
                if(!m_tlb_hit){
                    m_tlb_hit = true;
                }
                m_miss_queue.push(mf);
                m_l1_to_l2_num++;
            }
            return MISS;
        }
        
    }
    printf("some status wrong\n");
    return MISS;
}

void l1_data_cache::l1_cache_cycle(int cycles) {
    m_miss_queue_cycle(cycles);
    //process l1 request
    for (int bank = 0; bank < m_cache_config.m_n_banks; bank++) {
        if (!m_sm_to_l1[bank].empty()) {
            mem_fetch *mf = m_sm_to_l1[bank].front();
            unsigned  status = cache_access(mf->m_address, mf, cycles);
            if(status == RESERVATION_FAIL)
                continue;
            else{
                m_gpu->address_to_sms[mf->m_address].insert(sm_id);
                m_gpu->total_access++;
                m_sm_to_l1[bank].pop();
            }
        }
    }
}

void l1_data_cache::m_miss_queue_cycle(int cycles) {
    if(m_miss_queue.empty())
        return;
    mem_fetch* mf_next = m_miss_queue.front();
    unsigned request_size = mf_next->m_packet_size;
    unsigned output_node = mf_next->get_mem_sub_partition_id(std::stoi(m_gpu_config.m_gpu_config["mem_num"])/2,std::stoi(m_gpu_config.m_gpu_config["sm_num"]));
    if(m_gpu->m_icnt->HasBuffer(sm_id,request_size)){ // L1 icnt push time
        m_gpu->m_icnt->Push(sm_id,output_node,mf_next,request_size);
        m_gpu->l2_push_num++;
        m_miss_queue.pop();
        mf_next->m_pushed_time = cycles;
    }
}


void l1_data_cache::cache_fill(mem_fetch *mf, unsigned time) {
    m_tag_array->tag_array_fill(mf->m_address, time, mf->m_sector_mask);
}

void l2_data_cache::l2_cache_cycle(int cycles) {
    if(!m_dram_L2_queue->empty()){
        mem_fetch *mf = m_dram_L2_queue->top();
        m_gpu->m_icnt->Push(m_device_id,mf->m_sm_id,mf, mf->m_packet_size);


        new_addr_type mshr_address = mf->m_sector_address;
        while (m_mshrs->probe(mshr_address)){
            mem_fetch *mf_fill = m_mshrs->next_access(mshr_address);
            cache_fill(mf_fill, cycles); // L2 fill
        }
        m_dram_L2_queue->pop();
    }

    // L2 cache return
    mem_fetch *mf_temp = NULL;
    for (int i = 0; i < 3; i++) {
        if (!l2_to_sms[i].empty()) {
            mf_temp = l2_to_sms[i].front();
            while (mf_temp->m_l2_ready_time <= m_gpu->get_gpu_sim_cycles()) {
                l2_to_sms[i].pop();
                m_gpu->m_icnt->Push(m_device_id,mf_temp->m_sm_id,mf_temp,mf_temp->m_packet_size);

                if(mf_temp->m_status != L2_HIT){
                    new_addr_type mshr_address = mf_temp->m_sector_address;
                    while (m_mshrs->probe(mshr_address)){
                        mem_fetch *mf_fill = m_mshrs->next_access(mshr_address);
                        cache_fill(mf_fill, cycles); // L2 fill
                    }
                }
                if (!l2_to_sms[i].empty()) mf_temp = l2_to_sms[i].front();
                else break;
            }
        }
    }


    auto *mf = (mem_fetch *)m_gpu->m_icnt->Pop(m_device_id); // L2 receive packet from L1
    if(mf != nullptr){
        m_l1_to_l2.push(mf);
        m_gpu->l2_pop_num++;
    }

    if(!m_l1_to_l2.empty()){
        mem_fetch *mf_next = m_l1_to_l2.front();

        unsigned  status = cache_access(mf_next->m_address, mf_next, cycles);
        if(status != RESERVATION_FAIL)
            m_l1_to_l2.pop();
    }

}

unsigned l2_data_cache::cache_access(new_addr_type addr, mem_fetch *mf, unsigned int time) { 
    bool wr = mf->is_write();
    bool wb;
    if (wr) {
        m_write_cache_access_num++;
    } else {
        m_read_cache_access_num++;
    }
    new_addr_type block_addr = m_cache_config.block_addr(addr);
    auto cache_index = (unsigned) -1; //if hit index miss next evict index

    unsigned probe_status = m_tag_array->tag_array_probe(addr, mf->m_sector_mask, mf, cache_index);
    if (wr) {

        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_store_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
            sector_cache_block *block = m_tag_array->get_block(cache_index);
            block->set_status(MODIFIED, mf->m_sector_mask);
            if(mf->m_write_type == WRITE_THROUGH_L2){
                delete mf;
            }else{
                if(mf->m_tlb_hit){
                    send_l2_to_sm(mf, m_gpu->get_gpu_sim_cycles(),
                        std::stoi(m_gpu_config.m_gpu_config["l2_hit_latency"]));
                } else{
                    send_l2_to_sm(mf, m_gpu->get_gpu_sim_cycles(),
                        std::stoi(m_gpu_config.m_gpu_config["tlb_miss_latency"]));
                }
                mf->m_status = L2_HIT;
            }
            return HIT;
        } else { 
            int m_cache_index;
            m_tag_array->tag_array_probe_idle(block_addr,mf->m_sector_mask, mf, m_cache_index);
            if(m_cache_index>=0){
                m_store_sector_hit++;
                sector_cache_block *block = m_tag_array->get_block(m_cache_index);
                if(probe_status==MISS){
                    block->allocate(m_tag_array->m_config.tag(addr), m_tag_array->m_config.block_addr(addr),
                                    time, mf->m_sector_mask);
                } else if(probe_status==SECTOR_MISS){
                    block->allocate_sector(time, mf->m_sector_mask);
                }
                block->set_status(MODIFIED, mf->m_sector_mask);
                block->set_last_access_time(time, mf->m_sector_mask);
                if(mf->m_write_type == WRITE_THROUGH_L2){
                    delete mf;
                }else{
                    send_l2_to_dram(mf, std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]));
                    mf->m_status = L2_HIT;
                }
                return HIT;
            }else{
                new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
                bool mshr_hit = m_mshrs->probe(mshr_addr);
                if (mshr_hit) {
                    m_store_sector_hit++;
                }else{
                    m_store_sector_miss++;
                }
                if(mf->m_write_type == WRITE_THROUGH_L2){
                    delete mf;
                }else{
                    send_l2_to_dram(mf, std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]));
                    mf->m_status = L2_MISS;
                }
                return MISS;
            }
        }
    } else {
        if (probe_status == RESERVATION_FAIL) {
            return RESERVATION_FAIL;
        } else if (probe_status == HIT) {
            m_load_sector_hit++;
            m_tag_array->tag_array_access(block_addr, time, mf, cache_index);

            send_l2_to_sm(mf, m_gpu->get_gpu_sim_cycles(),
                             std::stoi(m_gpu_config.m_gpu_config["l2_hit_latency"]));
            mf->m_status = L2_HIT;
            return HIT;
        } else {
            new_addr_type mshr_addr = m_cache_config.mshr_addr(mf->m_sector_address);
            bool mshr_hit = m_mshrs->probe(mshr_addr);
            bool mshr_avail = !m_mshrs->full(mshr_addr);
            if (!mshr_avail) {
                return RESERVATION_FAIL;
            }
            if (mshr_hit) {
                m_load_sector_hit++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                m_mshrs->add(mshr_addr, mf);
                send_l2_to_dram(mf, std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]));
            } else {
                m_load_sector_miss++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index);
                m_mshrs->add(mshr_addr, mf);
                if(mf->m_tlb_hit){
                    send_l2_to_dram(mf, std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]));
                } else{
                    send_l2_to_dram(mf, std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]));
                }
                m_sector_load_to_next++;
                m_tag_array->tag_array_access(block_addr, time, mf, cache_index,wb);
            }
            mf->m_status = L2_MISS;
            return MISS;
        }
    }
}

void l2_data_cache::cache_fill(mem_fetch *mf, unsigned int time) {
    m_tag_array->tag_array_fill(mf->m_address, time, mf->m_sector_mask);
}


void memory_partition::init(gpu* gpu) {
    m_gpu = gpu;
    for(int i = 0; i< std::stoi(m_gpu_config.m_gpu_config["mem_num"]); i++){
        l2_data_cache* l2 = new l2_data_cache(m_cache_config,m_gpu_config,i,gpu);
        l2->init(this);
        m_l2_caches.push_back(l2);
    }
}

void memory_partition::dram_model_cycle(int subpid) {
  if (!m_dram_latency_queue[subpid].empty() &&
      (m_gpu->get_gpu_sim_cycles() >=
       m_dram_latency_queue[subpid].front().ready_cycle)) {
      auto d = m_dram_latency_queue[subpid].front();
    mem_fetch *mf_return = m_dram_latency_queue[subpid].front().req;
    if(!m_l2_caches[d.l2_id]->dram_L2_queue_full()){
        m_l2_caches[d.l2_id]->dram_L2_queue_push(mf_return);
        m_dram_latency_queue[subpid].pop_front();
        FILE *pFile;
        pFile = fopen ("./sim_out/mf_trace.out","a");
        if(pFile == nullptr){
            perror("ERROR: Failed to open file \"./sim_out/mf_trace.out\"!\n");
            exit(1);
        }
        fprintf(pFile, "send mf %-16u from DRAM to L2 %d\n", mf_return->m_mf_id, d.l2_id);
        fclose(pFile);
    }

  }

    int spid;
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        spid = (p + last_issued_partition[subpid] + 1) %
                m_config->m_n_sub_partition_per_memory_channel; // subpartition id 0 or 1

        if (!m_l2_caches[spid + subpid * m_config->m_n_sub_partition_per_memory_channel]->L2_dram_queue_empty() &&
            can_issue_to_dram(spid + subpid * 2)) {
          mem_fetch *mf = m_l2_caches[spid + subpid * 2]->L2_dram_queue_top();
          if (m_dram[subpid]->full()) break;

          m_l2_caches[spid + subpid * m_config->m_n_sub_partition_per_memory_channel]->L2_dram_queue_pop();
          dram_delay_t d{};
          d.req = mf;
          d.ready_cycle = m_gpu->get_gpu_sim_cycles() + std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]);
          d.l2_id = spid + subpid * m_config->m_n_sub_partition_per_memory_channel;
          m_dram_latency_queue[subpid].push_back(d);
          break;  
        }
    }

    last_issued_partition[subpid] = spid;
}

bool l2_data_cache::L2_dram_queue_empty() const {
    return m_L2_dram_queue->empty();
}

class mem_fetch *l2_data_cache::L2_dram_queue_top() const {
    return m_L2_dram_queue->top();
}

void l2_data_cache::L2_dram_queue_pop() { m_L2_dram_queue->pop(); }

bool l2_data_cache::dram_L2_queue_full() const {
    return m_dram_L2_queue->full();
}

void l2_data_cache::dram_L2_queue_push(class mem_fetch *mf) {
    m_dram_L2_queue->push(mf);
}

bool memory_partition::can_issue_to_dram(int inner_sub_partition_id) {
    return !m_l2_caches[inner_sub_partition_id]->dram_L2_queue_full();
}

memory_partition::memory_partition(const cache_config &cache_config, const gpu_config &gpu_config,
                                   const memory_config *config, const gpu* gpu):
    m_cache_config(cache_config),
    m_gpu_config(gpu_config),
    m_config(config),
    m_gpu(gpu){
        m_dram = new dram_t* [std::stoi(m_gpu_config.m_gpu_config["mem_num"]) / m_config->m_n_sub_partition_per_memory_channel]; //  the number of DRAM
        for(int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["mem_num"])  / m_config->m_n_sub_partition_per_memory_channel; i++){
            m_dram[i] = new dram_t(config, m_gpu, i);
        }
        m_dram_latency_queue = new std::list<dram_delay_t>[std::stoi(m_gpu_config.m_gpu_config["mem_num"])  / m_config->m_n_sub_partition_per_memory_channel];

        last_issued_partition = new int[std::stoi(m_gpu_config.m_gpu_config["mem_num"]) / m_config->m_n_sub_partition_per_memory_channel];
        for(int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["mem_num"]) / m_config->m_n_sub_partition_per_memory_channel; i++){
            last_issued_partition[i] = 0;
        }
    }

void memory_partition::dram_cycle(int subpid) {
    mem_fetch *mf_return = m_dram[subpid]->return_queue_top();
    if (mf_return) {
        int l2_cache_id = mf_return->l2_id;
        if(!m_l2_caches[l2_cache_id]->dram_L2_queue_full()){
            m_l2_caches[l2_cache_id]->dram_L2_queue_push(mf_return); // mf返回L2
            m_dram[subpid]->return_queue_pop();
        }
    }


    m_dram[subpid]->cycle();

    int spid;
    for (unsigned p = 0; p < m_config->m_n_sub_partition_per_memory_channel; p++) {
        spid = (p + last_issued_partition[subpid] + 1) %
               m_config->m_n_sub_partition_per_memory_channel; 

        if (!m_l2_caches[spid + subpid * 2]->L2_dram_queue_empty() &&
            can_issue_to_dram(spid + subpid * 2)) {
            mem_fetch *mf = m_l2_caches[spid + subpid * 2]->L2_dram_queue_top();
            if (m_dram[subpid]->full()) break;

            m_l2_caches[spid + subpid * 2]->L2_dram_queue_pop();
            dram_delay_t d{};
            mf->l2_id = spid + subpid * 2;
            d.req = mf;
            d.ready_cycle = m_gpu->get_gpu_sim_cycles() + std::stoi(m_gpu_config.m_gpu_config["l2_miss_latency"]);
            d.l2_id = spid + subpid * 2;
            m_dram_latency_queue[subpid].push_back(d);
            break; 
        }
    }

    // DRAM latency queue
    if (!m_dram_latency_queue[subpid].empty() &&
        (m_gpu->get_gpu_sim_cycles() >=
         m_dram_latency_queue[subpid].front().ready_cycle) &&
        !m_dram[subpid]->full()) {
        mem_fetch *mf = m_dram_latency_queue[subpid].front().req;
        m_dram_latency_queue[subpid].pop_front();
        m_dram[subpid]->push(mf);
    }
}


void memory_partition::memory_partition_cycle(int cycles) {
    for(int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["mem_num"]) / m_config->m_n_sub_partition_per_memory_channel; i++){
        if(m_config->simple_dram_model)
            dram_model_cycle(i);
        else
            dram_cycle(i);
    }

    int access_num = 0;
    for(int i = 0; i< std::stoi(m_gpu_config.m_gpu_config["mem_num"]); i++){
        m_l2_caches[i]->l2_cache_cycle(cycles);
        access_num += m_l2_caches[i]->m_read_cache_access_num;
    }
}

void memory_partition::print_stats(){
    for(int i = 0; i < std::stoi(m_gpu_config.m_gpu_config["mem_num"]) / m_config->m_n_sub_partition_per_memory_channel; ++i){
        m_dram[i]->print_stats();
    }
}

#endif
