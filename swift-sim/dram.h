//
// Created by liu on 3/13/2023.
//

#ifndef DRAM_H
#define DRAM_H

#include "delayqueue.h"
#include "mem_fetch.h"

class memory_config;

#define READ 'R'  // define read and write states
#define WRITE 'W'
#define BANK_IDLE 'I'
#define BANK_ACTIVE 'A'

enum bank_grp_bits_position { HIGHER_BITS = 0, LOWER_BITS };


class dram_req_t {
public:
    dram_req_t(class mem_fetch *data, unsigned banks,
               unsigned dram_bnk_indexing_policy, const gpu *);

    unsigned int row;
    unsigned int col;
    new_addr_type bk;
    unsigned int nbytes;
    unsigned int txbytes;
    unsigned int dqbytes;
    unsigned int age;
    unsigned int timestamp;
    unsigned char rw;  // is the request a read or a write?
    unsigned long long int addr;
    unsigned int insertion_time;
    class mem_fetch *data;
    const gpu *m_gpu;
};

struct bankgrp_t {
    unsigned int CCDLc;
    unsigned int RTPLc;
};

struct bank_t {
    unsigned int RCDc;
    unsigned int RCDWRc;
    unsigned int RASc;
    unsigned int RPc;
    unsigned int RCc;
    unsigned int WTPc;  // write to precharge
    unsigned int RTPc;  // read to precharge

    unsigned char rw;     // is the bank reading or writing?
    unsigned char state;  // is the bank active or idle?
    unsigned int curr_row;

    dram_req_t *mrq;

    unsigned int n_access;
    unsigned int n_writes;
    unsigned int n_idle;

    unsigned int bkgrpindex;
};

class dram_t {
public:

    dram_t(const memory_config *config, const gpu* gpu, unsigned id);

    bool full() const;

    class mem_fetch *return_queue_pop();
    class mem_fetch *return_queue_top();

    void push(class mem_fetch *data);

    void cycle();

    void print_stats();

    const gpu *m_gpu;
    unsigned int id;
    const memory_config *m_config;


private:
    bankgrp_t **bkgrp;
    bank_t **bk;
    unsigned int prio;

    unsigned get_bankgrp_number(unsigned i);

    void scheduler_fifo();
    void scheduler_frfcfs();

    bool issue_col_command(int j);
    bool issue_row_command(int j);

    unsigned int RRDc;
    unsigned int CCDc;
    unsigned int RTWc;  // read to write penalty applies across banks
    unsigned int WTRc;  // write to read penalty applies across banks

    unsigned char
            rw;  // was last request a read or write? (important for RTW, WTR)

    fifo_pipeline<dram_req_t> *rwq; // read and write queue
    fifo_pipeline<dram_req_t> *mrqq; // memory request queue
    // buffer to hold packets when DRAM processing is over
    // should be filled with dram clock and popped with l2 or icnt clock
    fifo_pipeline<mem_fetch> *returnq;

    unsigned long long n_act;
    unsigned long long n_pre;
    unsigned int bwutil;


    class frfcfs_scheduler *m_frfcfs_scheduler;


    unsigned int n_act_partial;
    unsigned int n_pre_partial;
    unsigned int bwutil_partial;


    friend class frfcfs_scheduler;

    unsigned *bank_accesses;
};
#endif 
