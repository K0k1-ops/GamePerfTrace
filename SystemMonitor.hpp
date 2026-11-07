#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct NetworkMetrics {
    float download_kbps = 0.0f;
    float upload_kbps = 0.0f;
};

struct DiskMetrics {
    float read_kbps = 0.0f;
    float write_kbps = 0.0f;
};

struct GpuMetrics {
    float usage_pct = 0.0f;
    float vram_used_mb = 0.0f;
    float vram_total_mb = 0.0f;
    float temp_c = 0.0f;
    std::string name = "Desconocido";
    bool available = false;
};

struct DiskDriveInfo {
    std::string mount_point;
    uint64_t total_gb = 0;
    uint64_t free_gb = 0;
};

struct SystemInfo {
    std::string cpu_model;
    std::string cpu_vendor;
    uint32_t cpu_cores = 0;
    std::string cpu_freq_mhz;
    std::string cpu_cache_l3;
    
    std::string gpu_vendor;
    std::string gpu_renderer;
    std::string gpu_version;
    std::string gpu_glsl_version;

    uint64_t total_ram_mb = 0;
    uint64_t total_swap_mb = 0;
    
    std::string os_name;
    std::string kernel_version;
    std::string architecture;
    std::string hostname;

    std::vector<DiskDriveInfo> drives;
};

class SystemMonitor {
public:
    SystemMonitor();
    ~SystemMonitor();

    float getCpuUsage();
    float getRamUsageMB();
    float getCpuTemperature();
    NetworkMetrics getNetworkIO();
    DiskMetrics getDiskIO();
    GpuMetrics getGpuMetrics();
    SystemInfo getStaticSystemInfo();
    uint64_t getUptimeSeconds();

private:
    uint64_t last_idle_time = 0;
    uint64_t last_total_time = 0;

    uint64_t last_net_rx = 0;
    uint64_t last_net_tx = 0;
    double last_net_time = 0.0;

    uint64_t last_disk_read = 0;
    uint64_t last_disk_write = 0;
    double last_disk_time = 0.0;

    void* nvml_handle = nullptr;
    bool nvml_initialized = false;
    void initNvml();
    GpuMetrics getNvidiaGpuMetrics();
    GpuMetrics getAmdIntelGpuMetrics();
    GpuMetrics getWindowsDxgiGpuMetrics();
};