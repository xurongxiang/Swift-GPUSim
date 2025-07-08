#include <iostream>
#include <pthread.h>
#include "gpu.h"

#include <chrono>
#define TRACE_PATH "/mnt/sda/xu/gpu_workload_traces/"

typedef std::chrono::high_resolution_clock Clock;


void *sim_gpu(void *thread_arg);

struct thread_data thread_data_array[6];

int main(int argc,char **argv) {
    int kernel_num, kernel_begin;
    std::string benchmark;
  
    if(argc < 3){
	std::cerr << "ERROR: missing parameter(s)" << std::endl;
        exit(1);
    }else if(argc > 4){
        std::cerr << "ERROR: redundant parameter(s)" << std::endl; 
        exit(1);
    }else if(argc == 3){
        benchmark.append(argv[1]);
        kernel_num = std::stoi(argv[2]);
    }else if(argc == 4){
        benchmark.append(argv[1]);
        kernel_begin = std::stoi(argv[2]);
        kernel_num = std::stoi(argv[3]);
    }

    pthread_t threads[kernel_num];
    if(argc == 3){
        for (int i = 0; i < kernel_num; i++) {
            thread_data_array[i].kernel_id = i + 1;
            thread_data_array[i].benchmark = benchmark;
            int rc = pthread_create(&threads[i], nullptr,
                           sim_gpu, (void *) &(thread_data_array[i]));
            if (rc) {
                printf("ERROR: return code from pthread_create() is %d\n", rc);
                exit(-1);
            }
        }
    }
    else if(argc == 4){
        for (int i = kernel_begin; i < kernel_num; i++) {
            thread_data_array[i].kernel_id = i + 1;
            thread_data_array[i].benchmark = benchmark;
            int rc = pthread_create(&threads[i], nullptr,
                           sim_gpu, (void *) &(thread_data_array[i]));
            if (rc) {
                printf("ERROR: return code from pthread_create() is %d\n", rc);
                exit(-1);
            }
        }
    }
    pthread_exit(nullptr);
}

void *sim_gpu(void *thread_arg) {
    int kernel_id = ((struct thread_data*) thread_arg)->kernel_id;
    std::string benchmark = ((struct thread_data*) thread_arg)->benchmark;
    std::string trace_path = TRACE_PATH + benchmark + "/traces";
    std::ifstream file_sass(trace_path + "/kernel-" + std::to_string(kernel_id) + ".sass");
    std::ifstream file_mem(trace_path + "/kernel-" + std::to_string(kernel_id) + ".mem");
    if(!file_mem.is_open()){
        std::cerr << "ERROR: mem trace missing " << std::endl;
        pthread_exit(nullptr);
    }
    if(!file_sass.is_open()){
        std::cerr << "ERROR: sass trace missing " << std::endl;
        pthread_exit(nullptr);
    }
    gpu m_gpu = gpu(benchmark);
    std::ifstream file(trace_path + "/kernel-" + std::to_string(kernel_id) + "-block-0.mem");
    if (!file.is_open()) {
        m_gpu.traceReader->gen_block_mem_trace(trace_path, kernel_id);
    }
    m_gpu.launch_kernel(kernel_id);
    m_gpu.execute_kernel(kernel_id);
    auto t1 = Clock::now();
    m_gpu.gpu_cycle((void *) thread_arg);
    auto t2 = Clock::now();
    std::chrono::nanoseconds t21 = t2 - t1;
    long kernel_time = std::chrono::duration_cast<std::chrono::microseconds>(t21).count();
    printf("total_time %ld us\n", kernel_time);
    pthread_exit(nullptr);
}
