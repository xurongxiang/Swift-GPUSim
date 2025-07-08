#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <map>
#include <string>


class gpu_config {
public:
    gpu_config() = default;
    gpu_config(const gpu_config& o){
        m_gpu_config = o.m_gpu_config;
        m_sm_pipeline_units = o.m_sm_pipeline_units;
        m_compute_capability = o.m_compute_capability;
        l1_cache_config = o.l1_cache_config;
        l2_cache_config = o.l2_cache_config;
        gpu_isa_latency = o.gpu_isa_latency;
    }

    void read_config(const std::string &file_s);

    static void config_split(const std::string &str, const std::string &pattern, std::string &str1, std::string &str2);

    void read_m_config() {
        read_config("/mnt/sda/xu/bishe/swiftsim/single-chip/swift-sim/gpu.config");
        read_config("/mnt/sda/xu/bishe/swiftsim/single-chip/swift-sim/gpu_isa_latency.config");
    }

    std::map<std::string, std::string> m_gpu_config = {};
    std::map<std::string, int> m_sm_pipeline_units = {};
    std::map<std::string, int> m_compute_capability = {};
    std::map<std::string, std::string> l1_cache_config = {};
    std::map<std::string, std::string> l2_cache_config = {};
    std::map<std::string, int> memory_config = {};
    std::map<std::string, std::tuple<int, std::string> > gpu_isa_latency = {};
};

#endif 
