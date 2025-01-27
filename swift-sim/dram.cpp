//
// Created by liu on 3/13/2023.
//
#include "dram.h"
#include "cache.h"

#define DEC2ZERO(x) x = (x) ? (x - 1) : 0;


bool dram_t::full() const {
    return mrqq->full();
}

dram_t::dram_t(const memory_config *config, const gpu *gpu, unsigned id):
m_config(config),
m_gpu(gpu),
id(id){
    CCDc = 0;
    RRDc = 0;
    RTWc = 0;
    WTRc = 0;
    rw = READ;  // read mode is default
    bkgrp = (bankgrp_t **)calloc(sizeof(bankgrp_t *), m_config->nbkgrp);
    bkgrp[0] = (bankgrp_t *)calloc(sizeof(bank_t), m_config->nbkgrp);
    for (unsigned i = 1; i < m_config->nbkgrp; i++) {
        bkgrp[i] = bkgrp[0] + i;
    }
    for (unsigned i = 0; i < m_config->nbkgrp; i++) {
        bkgrp[i]->CCDLc = 0;
        bkgrp[i]->RTPLc = 0;
    }

    bk = (bank_t **)calloc(sizeof(bank_t *), m_config->nbk);
    bk[0] = (bank_t *)calloc(sizeof(bank_t), m_config->nbk);
    for (unsigned i = 1; i < m_config->nbk; i++) bk[i] = bk[0] + i;
    for (unsigned i = 0; i < m_config->nbk; i++) {
        bk[i]->state = BANK_IDLE; // 初始化為idle
        bk[i]->bkgrpindex = i / (m_config->nbk / m_config->nbkgrp);
    }
    prio = 0;

    rwq = new fifo_pipeline<dram_req_t>("rwq", m_config->CL, m_config->CL + 1);
    mrqq = new fifo_pipeline<dram_req_t>("mrqq", 0, 2);
    returnq = new fifo_pipeline<mem_fetch>(
            "dramreturnq", 0,
            m_config->gpgpu_dram_return_queue_size == 0
            ? 1024
            : m_config->gpgpu_dram_return_queue_size);
//    m_frfcfs_scheduler = NULL;
//    if (m_config->scheduler_type == DRAM_FRFCFS)
//        m_frfcfs_scheduler = new frfcfs_scheduler(m_config, this);
    bank_accesses = new unsigned[m_config->nbk]();
}

void dram_t::cycle() {
    if (!returnq->full()) {
        dram_req_t *cmd = rwq->pop();
        if (cmd) {
                mem_fetch *data = cmd->data;
                returnq->push(data);
        }
    }

    /* check if the upcoming request is on an idle bank */
    /* Should we modify this so that multiple requests are checked? */

    switch (m_config->scheduler_type) {
        case DRAM_FIFO:
            scheduler_fifo();
            break;
        case DRAM_FRFCFS:
//            scheduler_frfcfs();
            break;
        default:
            printf("Error: Unknown DRAM scheduler type\n");
            assert(0);
    }
    for (int i = 0; i < m_config->nbk; ++i) {
        if(bk[i]->mrq)
            rwq->push(bk[i]->mrq);
            bk[i]->mrq = nullptr;
    }


    /*
    unsigned k = m_config->nbk;

    bool issued_col_cmd = false;
    bool issued_row_cmd = false;

    if (m_config->dual_bus_interface) {
        // dual bus interface
        // issue one row command and one column command
        for (unsigned i = 0; i < m_config->nbk; i++) {
            unsigned j = (i + prio) % m_config->nbk;
            issued_col_cmd = issue_col_command(j);
            if (issued_col_cmd) break;
        }
        for (unsigned i = 0; i < m_config->nbk; i++) {
            unsigned j = (i + prio) % m_config->nbk;
            issued_row_cmd = issue_row_command(j);
            if (issued_row_cmd) break;
        }
        for (unsigned i = 0; i < m_config->nbk; i++) {
            unsigned j = (i + prio) % m_config->nbk;
            if (!bk[j]->mrq) {
                if (!CCDc && !RRDc && !RTWc && !WTRc && !bk[j]->RCDc && !bk[j]->RASc &&
                    !bk[j]->RCc && !bk[j]->RPc && !bk[j]->RCDWRc)
                    k--;
                bk[j]->n_idle++;
            }
        }
    } else {
        // single bus interface
        // issue only one row/column command
        for (unsigned i = 0; i < m_config->nbk; i++) {
            unsigned j = (i + prio) % m_config->nbk;
            if (!issued_col_cmd) issued_col_cmd = issue_col_command(j);

            if (!issued_col_cmd && !issued_row_cmd)
                issued_row_cmd = issue_row_command(j);

            if (!bk[j]->mrq) {
                if (!CCDc && !RRDc && !RTWc && !WTRc && !bk[j]->RCDc && !bk[j]->RASc &&
                    !bk[j]->RCc && !bk[j]->RPc && !bk[j]->RCDWRc)
                    k--;
                bk[j]->n_idle++;
            }
        }
    }

    // decrements counters once for each time dram_issueCMD is called

    DEC2ZERO(RRDc);
    DEC2ZERO(CCDc);
    DEC2ZERO(RTWc);
    DEC2ZERO(WTRc);
    for (unsigned j = 0; j < m_config->nbk; j++) {
        DEC2ZERO(bk[j]->RCDc);
        DEC2ZERO(bk[j]->RASc);
        DEC2ZERO(bk[j]->RCc);
        DEC2ZERO(bk[j]->RPc);
        DEC2ZERO(bk[j]->RCDWRc);
        DEC2ZERO(bk[j]->WTPc);
        DEC2ZERO(bk[j]->RTPc);
    }
    for (unsigned j = 0; j < m_config->nbkgrp; j++) {
        DEC2ZERO(bkgrp[j]->CCDLc);
        DEC2ZERO(bkgrp[j]->RTPLc);
    }
    */
}

void dram_t::push(struct mem_fetch *data) {
    auto *mrq =
            new dram_req_t(data, m_config->nbk, m_config->dram_bnk_indexing_policy, m_gpu);


    mrqq->push(mrq);
}

class mem_fetch *dram_t::return_queue_pop() {
    return returnq->pop();
}

class mem_fetch *dram_t::return_queue_top() {
    return returnq->top();
}

dram_req_t::dram_req_t(struct mem_fetch *mf, unsigned int banks, unsigned int dram_bnk_indexing_policy, const gpu *gpu) {
    data = mf;
    m_gpu = gpu;
    bk = data->bk;
    row = data->row;
    rw = data->m_access_type ? WRITE : READ;


}
void dram_t::scheduler_fifo() {
    if (!mrqq->empty()) {
        unsigned int bkn;
        dram_req_t *head_mrqq = mrqq->top();

        bkn = head_mrqq->bk;
        if (!bk[bkn]->mrq){
            bk[bkn]->mrq = mrqq->pop();
            bank_accesses[bkn]++;
        }
        else{
            // todo bank conflit

        }
    }
}

bool dram_t::issue_col_command(int j) {
    bool issued = false;
    unsigned grp = get_bankgrp_number(j);
    if (bk[j]->mrq) {  // if currently servicing a memory request

        // correct row activated for a READ
        if (!issued && !CCDc && !bk[j]->RCDc && !(bkgrp[grp]->CCDLc) &&
            (bk[j]->curr_row == bk[j]->mrq->row) && (bk[j]->mrq->rw == READ) &&
            (WTRc == 0) && (bk[j]->state == BANK_ACTIVE) && !rwq->full()) {
            if (rw == WRITE) {
                rw = READ;
                rwq->set_min_length(m_config->CL);
            }
            rwq->push(bk[j]->mrq);
            bk[j]->mrq->txbytes += m_config->dram_atom_size;
            CCDc = m_config->tCCD;
            bkgrp[grp]->CCDLc = m_config->tCCDL;
            RTWc = m_config->tRTW;
            bk[j]->RTPc = m_config->BL / m_config->data_command_freq_ratio;
            bkgrp[grp]->RTPLc = m_config->tRTPL;
            issued = true;


            bwutil += m_config->BL / m_config->data_command_freq_ratio;
            bwutil_partial += m_config->BL / m_config->data_command_freq_ratio;
            bk[j]->n_access++;

            bk[j]->mrq = nullptr;


        } else
            // correct row activated for a WRITE
        if (!issued && !CCDc && !bk[j]->RCDWRc && !(bkgrp[grp]->CCDLc) &&
            (bk[j]->curr_row == bk[j]->mrq->row) && (bk[j]->mrq->rw == WRITE) &&
            (RTWc == 0) && (bk[j]->state == BANK_ACTIVE) && !rwq->full()) {
            if (rw == READ) {
                rw = WRITE;
                rwq->set_min_length(m_config->WL);
            }
            rwq->push(bk[j]->mrq);
            CCDc = m_config->tCCD;
            bkgrp[grp]->CCDLc = m_config->tCCDL;
            WTRc = m_config->tWTR;
            bk[j]->WTPc = m_config->tWTP;
            issued = true;


            bwutil += m_config->BL / m_config->data_command_freq_ratio;
            bwutil_partial += m_config->BL / m_config->data_command_freq_ratio;

            bk[j]->mrq = nullptr;

        }
    }

    return issued;
}

bool dram_t::issue_row_command(int j) {
    bool issued = false;
    unsigned grp = get_bankgrp_number(j);
    if (bk[j]->mrq) {  // if currently servicing a memory request

        //     bank is idle
        // else
        if (!issued && !RRDc && (bk[j]->state == BANK_IDLE) && !bk[j]->RPc &&
            !bk[j]->RCc) {  //
#ifdef DRAM_VERIFY
            PRINT_CYCLE = 1;
      printf("\tACT BK:%d NewRow:%03x From:%03x \n", j, bk[j]->mrq->row,
             bk[j]->curr_row);
#endif
            // activate the row with current memory request
            bk[j]->curr_row = bk[j]->mrq->row;
            bk[j]->state = BANK_ACTIVE;
            RRDc = m_config->tRRD;
            bk[j]->RCDc = m_config->tRCD;
            bk[j]->RCDWRc = m_config->tRCDWR;
            bk[j]->RASc = m_config->tRAS;
            bk[j]->RCc = m_config->tRC;
            prio = (j + 1) % m_config->nbk;
            issued = true;
            n_act_partial++;
            n_act++;
        }

        else
            // different row activated
        if ((!issued) && (bk[j]->curr_row != bk[j]->mrq->row) &&
            (bk[j]->state == BANK_ACTIVE) &&
            (!bk[j]->RASc && !bk[j]->WTPc && !bk[j]->RTPc &&
             !bkgrp[grp]->RTPLc)) {
            // make the bank idle again
            bk[j]->state = BANK_IDLE;
            bk[j]->RPc = m_config->tRP;
            prio = (j + 1) % m_config->nbk;
            issued = true;
            n_pre++;
            n_pre_partial++;
#ifdef DRAM_VERIFY
            PRINT_CYCLE = 1;
      printf("\tPRE BK:%d Row:%03x \n", j, bk[j]->curr_row);
#endif
        }
    }
    return issued;
}

unsigned dram_t::get_bankgrp_number(unsigned i) {
    if (m_config->dram_bnkgrp_indexing_policy ==
               LOWER_BITS) {  // lower bits
        return i & ((m_config->nbkgrp - 1));
    } else {
        assert(1);
    }
}

void dram_t::print_stats(){
//    FILE *pFile; // todo
//    pFile = fopen ("./sim_out/bk_trace.out","a");
//    if(pFile == nullptr){
//        perror("ERROR: Failed to open file \"./sim_out/mf_trace.out\"!\n");
//        exit(1);
//    }
//    fprintf(pFile, "DRAM %d\n", id);
//    for (int i = 0; i < m_config->nbk; ++i) {
//        fprintf(pFile, "bk[%d]: %d  ", i, bank_accesses[i]);
//    }
//    fprintf(pFile, "\n");
//    fclose(pFile);
}
