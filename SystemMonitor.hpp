#ifndef SYSTEM_MONITOR_HPP
#define SYSTEM_MONITOR_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

struct StorageDrive {
    std::string mount_point;
    uint64_t total_gb = 0;
    uint64_t free_gb = 0;
};

struct NetworkMetrics {
    float download_kbps = 0.0f;
    float upload_kbps = 0.0f;
};

struct DiskMetrics {
    float read_kbps = 0.0f;
    float write_kbps = 0.0f;
};

struct SystemInfo {
    // CPU
    std::string cpu_model = "Desconocido";
    std::string cpu_vendor = "Desconocido";
    std::string cpu_cache_l3 = "Desconocido";
    std::string cpu_freq_mhz = "Desconocido";
    unsigned int cpu_cores = 1;

    // GPU (Se completa con OpenGL en runtime)
    std::string gpu_vendor = "Desconocido";
    std::string gpu_renderer = "Desconocido";
    std::string gpu_version = "Desconocido";
    std::string gpu_glsl_version = "Desconocido";

    // OS & Kernel
    std::string os_name = "Desconocido";
    std::string kernel_version = "Desconocido";
    std::string hostname = "Desconocido";
    std::string architecture = "Desconocido";

    // RAM & Swap
    uint64_t total_ram_mb = 0;
    uint64_t total_swap_mb = 0;

    // Storage
    std::vector<StorageDrive> drives;
};

class SystemMonitor {
public:
    SystemMonitor();
    
    uint64_t getRamUsageMB();
    float getCpuUsage();
    unsigned int getCpuCores();
    float getCpuTemperature();
    uint64_t getUptimeSeconds();
    
    SystemInfo getStaticSystemInfo();
    NetworkMetrics getNetworkIO();
    DiskMetrics getDiskIO();

private:
#ifdef _WIN32
    uint64_t prev_idle_time = 0;
    uint64_t prev_kernel_time = 0;
    uint64_t prev_user_time = 0;
#else
    uint64_t prev_user = 0, prev_nice = 0, prev_system = 0, prev_idle = 0;
    uint64_t prev_iowait = 0, prev_irq = 0, prev_softirq = 0, prev_steal = 0;

    uint64_t prev_rx_bytes = 0;
    uint64_t prev_tx_bytes = 0;
    uint64_t prev_read_bytes = 0;
    uint64_t prev_write_bytes = 0;
    std::chrono::steady_clock::time_point last_net_time;
    std::chrono::steady_clock::time_point last_disk_time;
#endif

    uint64_t fileTimeToUint64(const void* ft);
};

#endif