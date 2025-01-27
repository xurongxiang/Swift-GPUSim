//
// Created by 徐向荣 on 2022/6/13.
//

#include "mem_fetch.h"
#include "gpu.h"

mem_fetch::mem_fetch(unsigned sm_id, bool access_type, new_addr_type address, unsigned sector_mask, unsigned space,
          bool is_atomic, long long gen_time, int src_block_id, int src_warp_id, int completion_index,
          gpu*p_gpu) {
    m_sm_id = sm_id;
    m_access_type = access_type;
    m_address = address; 
    m_sector_mask = sector_mask;
    m_space = space;
    m_is_atomic = is_atomic;
    m_gen_time = gen_time;
    m_src_block_id = src_block_id;
    m_src_warp_id = src_warp_id;
    m_completion_index = completion_index;
    m_status = INIT;
    m_write_type = NORMAL;
    m_sector_address = m_address + sector_mask * SECTOR_SIZE;
    m_packet_size = 0; 
    m_l1_ready_time = 0;
    m_l2_ready_time = 0;
    m_gpu = p_gpu;
    m_mf_id = m_gpu->get_mf_id();
    m_gpu->increase_mf_id();
    pc = -1;
    pc_index = -1;
}

int mem_fetch::get_mem_sub_partition_id(int mem_channel,int sm_num){
    new_addr_type addr = m_sector_address;
    int ADDR_CHIP_S = 10, m_n_channel = mem_channel, BANk_MASK = 0x300, ADDR_BANK_S = 8;

    addrdec_mask[CHIP]  = 0x0000000000000000;        // high:64 low:0
    addrdec_mask[BK]    = 0x0000000000007080;       // high:15 low:7
    addrdec_mask[ROW]   = 0x000000000fff8000;       //  high:28 low:15
    addrdec_mask[COL]   = 0x0000000000000f7f;       //  high:12 low:0
    addrdec_mask[BURST] = 0x000000000000001f;       //  high:5 low:0
    addrdec_getmasklimit(addrdec_mask[CHIP], &addrdec_mkhigh[CHIP], &addrdec_mklow[CHIP]);
    addrdec_getmasklimit(addrdec_mask[BK], &addrdec_mkhigh[BK], &addrdec_mklow[BK]);
    addrdec_getmasklimit(addrdec_mask[ROW], &addrdec_mkhigh[ROW], &addrdec_mklow[ROW]);
    addrdec_getmasklimit(addrdec_mask[COL], &addrdec_mkhigh[COL], &addrdec_mklow[COL]);
    addrdec_getmasklimit(addrdec_mask[BURST], &addrdec_mkhigh[BURST], &addrdec_mklow[BURST]);



    unsigned long long int addr_for_chip, rest_of_addr, rest_of_addr_high_bits;
    unsigned long long int PARTITION_PER_CHANNEL = 2;
    // not 2^n partitions
    addr_for_chip = (addr >> ADDR_CHIP_S) % m_n_channel;
    rest_of_addr = ((addr >> ADDR_CHIP_S) / m_n_channel) << ADDR_CHIP_S;
    rest_of_addr_high_bits = ((addr >> ADDR_CHIP_S) / m_n_channel);
    rest_of_addr |= addr & ((1 << ADDR_CHIP_S) - 1);

    chip = addr_for_chip;
    bk = addrdec_packbits(addrdec_mask[BK], rest_of_addr, addrdec_mkhigh[BK], addrdec_mklow[BK]);
    row = addrdec_packbits(addrdec_mask[ROW], rest_of_addr, addrdec_mkhigh[ROW], addrdec_mklow[ROW]);
    return (int)(chip * PARTITION_PER_CHANNEL + (bk & (PARTITION_PER_CHANNEL - 1))) + sm_num;
}

static new_addr_type addrdec_packbits(new_addr_type mask, new_addr_type val,
                                      unsigned char high, unsigned char low) {
    unsigned pos = 0;
    new_addr_type result = 0;
    for (unsigned i = low; i < high; i++) {
        if ((mask & ((unsigned long long int)1 << i)) != 0) {
            result |= ((val & ((unsigned long long int)1 << i)) >> i) << pos;
            pos++;
        }
    }
    return result;
}

static void addrdec_getmasklimit(new_addr_type mask, unsigned char *high,
                                 unsigned char *low) {
    *high = 64;
    *low = 0;
    int i;
    int low_found = 0;

    for (i = 0; i < 64; i++) {
        if ((mask & ((unsigned long long int)1 << i)) != 0) {
            if (low_found) {
                *high = i + 1;
            } else {
                *high = i + 1;
                *low = i;
                low_found = 1;
            }
        }
    }
}