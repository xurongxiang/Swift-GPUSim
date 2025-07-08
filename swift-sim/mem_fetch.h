//
// Created by 徐向荣 on 2022/6/13.
//

#ifndef MEM_FETCH_H
#define MEM_FETCH_H

#define SECTOR_SIZE  32

class gpu;
typedef unsigned long long new_addr_type;
enum mem_fetch_space {
    GLOBAL = 0,
    LOCAL,
    SHARE
};

enum mem_fetch_status {
    L1_HIT = 0,
    L1_MISS,
    L2_HIT,
    L2_MISS,
    INIT
};

enum mem_fetch_write_type {
    NORMAL = 0,
    WRITE_THROUGH,
    WRITE_BACK,
    WRITE_THROUGH_L2
};

static new_addr_type addrdec_packbits(new_addr_type mask, new_addr_type val,
                                      unsigned char high, unsigned char low);
static void addrdec_getmasklimit(new_addr_type mask, unsigned char *high,
                                 unsigned char *low);

class mem_fetch {
public:
    mem_fetch(unsigned sm_id, bool access_type, new_addr_type address, unsigned sector_mask, unsigned space,
              bool is_atomic, long long gen_time, int src_block_id, int src_warp_id, int completion_index,
              gpu*p_gpu);

    bool is_atomic() const {
        return m_is_atomic;
    }

    bool is_write() const {
        return m_access_type; //write true
    }

    int get_mem_sub_partition_id(int,int);

    unsigned m_sm_id;
    bool m_access_type; // 0: read 1: write
    new_addr_type m_address;
    unsigned m_sector_mask;
    unsigned m_space;
    bool m_is_atomic;
    long long m_gen_time;
    int m_status;
    int m_write_type;
    int m_src_block_id;
    int m_src_warp_id;
    int m_completion_index;
    new_addr_type m_sector_address;
    bool m_tlb_hit;
    unsigned m_packet_size;
    int m_l1_ready_time;
    int m_l2_ready_time;
    long long pc;
    int pc_index;

    enum { CHIP = 0, BK = 1, ROW = 2, COL = 3, BURST = 4, N_ADDRDEC };
    new_addr_type chip;
    new_addr_type bk;
    new_addr_type row;
    new_addr_type col;
    unsigned char addrdec_mklow[N_ADDRDEC];
    unsigned char addrdec_mkhigh[N_ADDRDEC];
    new_addr_type addrdec_mask[N_ADDRDEC];

    unsigned m_pushed_time;
    unsigned m_poped_time;
    gpu* m_gpu;
    unsigned m_mf_id;
    int l2_id;
};

#endif 
