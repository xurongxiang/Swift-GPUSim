#include "trace_reader.h"
#include "kernel.h" 

#include <iostream>
#include <cmath>
#include <vector>
#include <string>


void trace_reader::gen_block_mem_trace(const std::string &trace_path, int kernel_id) {
    std::string mem_file = trace_path + "/kernel-" + std::to_string(kernel_id) + ".mem";
    std::ifstream file(mem_file);
    std::string line;
    while (std::getline(file, line)) {
        unsigned pos_LDG = line.find("LDG");
        unsigned pos_STG = line.find("STG");
        unsigned pos_LD = line.find("LD.");
        unsigned pos_ST = line.find("ST.");

        unsigned pos_ATOM = line.find("ATOM");
        if (pos_LDG != std::string::npos || pos_STG != std::string::npos || pos_ATOM != std::string::npos || pos_LD != std::string::npos || pos_ST != std::string::npos) {
            unsigned pos_split = line.find_first_of(' ');
            std::string tmp = line.substr(0, pos_split);
            int block_id = std::stoi(tmp);
            std::ofstream wf(
                    trace_path + "/kernel-" + std::to_string(kernel_id) + "-block-" + std::to_string(block_id) +
                    ".mem", std::ios::app);
            line = line.substr(pos_split + 1, line.size() - 2);
            wf << line << "\n";
            wf.close();
        }
    }
    file.close();
}


void trace_reader::read_sass(const std::string &benchmark_path, kernel_info &kernelInfo,
                             std::vector<trace_inst> &trace_insts) const {
    std::string file_s = benchmark_path + "kernel-" + std::to_string(m_kernel_id) + ".sass";
    std::ifstream file(file_s);
    if(!file.is_open()){
        printf("%s does not exist!\n", file_s.c_str());
        exit(1);
    }
    std::string line;
    std::map<int, std::map<int, std::map<long long, int> >> block_pc_num; //block,warp,pc,pc_index
    while (std::getline(file, line)) {
        if (line == "Flexsim version 3 sass trace" || line == "Flexsim kernel info end" || line == "SwiftSim sass trace"||line == "SwiftSim kernel info end")
            continue;
        else if (line.find("kernel_id = ") != std::string::npos) {
            std::string str = "kernel_id = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_kernel_id = std::stoi(line);
        } else if (line.find("grid_size = ") != std::string::npos) {
            std::string str = "grid_size = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_grid_size = std::stoi(line);
        } else if (line.find("block_size = ") != std::string::npos) {
            std::string str = "block_size = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_block_size = std::stoi(line);
        } else if (line.find("shared_mem_bytes = ") != std::string::npos) {
            std::string str = "shared_mem_bytes = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_shared_mem_bytes = std::stoi(line);
        } else if (line.find("num_registers = ") != std::string::npos) {
            std::string str = "num_registers = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_num_registers = std::stoi(line);
        } else {
            std::istringstream is(line);
            std::string str;
            std::queue<std::string> tmp_str;
            while (is >> str) {
                tmp_str.push(str);
            }
            int block_id = std::stoi(tmp_str.front());
            tmp_str.pop();
            int warp_id = std::stoi(tmp_str.front());
            tmp_str.pop();
            char *stop;
            long long pc = std::strtoll(tmp_str.front().c_str(), &stop, 16);
            tmp_str.pop();
            std::string active_mask = tmp_str.front();
            tmp_str.pop();
            int dest_num = std::stoi(tmp_str.front());
            tmp_str.pop();
            std::vector<std::string> dest_regs;
            for (int i = 0; i < dest_num; i++) {
                dest_regs.push_back(tmp_str.front());
                tmp_str.pop();
            }
            std::string opcode = tmp_str.front();
            tmp_str.pop();
            int src_num = std::stoi(tmp_str.front());
            tmp_str.pop();
            std::vector<std::string> src_regs;
            for (int i = 0; i < src_num; i++) {
                src_regs.push_back(tmp_str.front());
                tmp_str.pop();
            }
            int pc_index = 0;
            if (block_pc_num.find(block_id) != block_pc_num.end() &&
                block_pc_num[block_id].find(warp_id) != block_pc_num[block_id].end()) { // block
                if (block_pc_num[block_id][warp_id].find(pc) != block_pc_num[block_id][warp_id].end()) {
                    pc_index = block_pc_num[block_id][warp_id][pc] + 1;
                    block_pc_num[block_id][warp_id][pc] = pc_index;
                } else {
                    block_pc_num[block_id][warp_id][pc] = 0;
                }
            } else {
                block_pc_num[block_id][warp_id][pc] = 0;
            }
            trace_inst m_inst = trace_inst(block_id, warp_id, pc, active_mask, dest_num, dest_regs, opcode, src_num,
                                           src_regs,
                                           pc_index);
            trace_insts.push_back(m_inst);
        }
    }
}

void trace_reader::read_all_block_sass(const string &benchmark_path, kernel_info &kernelInfo,
                                       std::vector<trace_inst> &trace_insts) const {
    std::string file_s = benchmark_path + "kernel-" + std::to_string(m_kernel_id) + ".allsass";
    std::ifstream file(file_s);
    if(!file.is_open()){
        printf("%s does not exist!\n", file_s.c_str());
        exit(1);
    }
    std::string line;
    std::map<int, std::map<int, std::map<long long, int> >> block_pc_num; //block,warp,pc,pc_index
    while (std::getline(file, line)) {
        if (line == "Flexsim version 3 sass trace" || line == "Flexsim kernel info end")
            continue;
        else if (line.find("kernel_id = ") != std::string::npos) {
            std::string str = "kernel_id = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_kernel_id = std::stoi(line);
        } else if (line.find("grid_size = ") != std::string::npos) {
            std::string str = "grid_size = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_grid_size = std::stoi(line);
        } else if (line.find("block_size = ") != std::string::npos) {
            std::string str = "block_size = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_block_size = std::stoi(line);
        } else if (line.find("shared_mem_bytes = ") != std::string::npos) {
            std::string str = "shared_mem_bytes = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_shared_mem_bytes = std::stoi(line);
        } else if (line.find("num_registers = ") != std::string::npos) {
            std::string str = "num_registers = ";
            line = line.replace(line.find(str), str.length(), "");
            kernelInfo.m_num_registers = std::stoi(line);
        } else {
            std::istringstream is(line);
            std::string str;
            std::queue<std::string> tmp_str;
            while (is >> str) {
                tmp_str.push(str);
            }
            int block_id = std::stoi(tmp_str.front());
            tmp_str.pop();
            int warp_id = std::stoi(tmp_str.front());
            tmp_str.pop();
            char *stop;
            long long pc = std::strtoll(tmp_str.front().c_str(), &stop, 16);
            tmp_str.pop();
            std::string active_mask = tmp_str.front();
            tmp_str.pop();
            int dest_num = std::stoi(tmp_str.front());
            tmp_str.pop();
            std::vector<std::string> dest_regs;
            for (int i = 0; i < dest_num; i++) {
                dest_regs.push_back(tmp_str.front());
                tmp_str.pop();
            }
            std::string opcode = tmp_str.front();
            tmp_str.pop();
            int src_num = std::stoi(tmp_str.front());
            tmp_str.pop();
            std::vector<std::string> src_regs;
            for (int i = 0; i < src_num; i++) {
                src_regs.push_back(tmp_str.front());
                tmp_str.pop();
            }
            int pc_index = 0;
            if (block_pc_num.find(block_id) != block_pc_num.end() &&
                block_pc_num[block_id].find(warp_id) != block_pc_num[block_id].end()) { // block
                if (block_pc_num[block_id][warp_id].find(pc) != block_pc_num[block_id][warp_id].end()) {
                    pc_index = block_pc_num[block_id][warp_id][pc] + 1;
                    block_pc_num[block_id][warp_id][pc] = pc_index;
                } else {
                    block_pc_num[block_id][warp_id][pc] = 0;
                }
            } else {
                block_pc_num[block_id][warp_id][pc] = 0;
            }
            trace_inst m_inst = trace_inst(block_id, warp_id, pc, active_mask, dest_num, dest_regs, opcode, src_num,
                                           src_regs,
                                           pc_index);
            trace_insts.push_back(m_inst);
        }
    }
}




