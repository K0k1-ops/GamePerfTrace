#include "SystemMonitor.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>
#include <cstring>

namespace fs = std::filesystem;

// Definición de tipos de funciones de NVML para carga dinámica
typedef int (*pfn_nvmlInit)(void);
typedef int (*pfn_nvmlShutdown)(void);
typedef int (*pfn_nvmlDeviceGetHandleByIndex)(unsigned int, void**);
typedef int (*pfn_nvmlDeviceGetUtilizationRates)(void*, void*);
typedef int (*pfn_nvmlDeviceGetMemoryInfo)(void*, void*);
typedef int (*pfn_nvmlDeviceGetTemperature)(void*, int, unsigned int*);
typedef int (*pfn_nvmlDeviceGetName)(void*, char*, unsigned int);

struct NvmlUtilization {
    unsigned int gpu;
    unsigned int memory;
};

struct NvmlMemory {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
};

SystemMonitor::SystemMonitor() {
    getCpuUsage();
    getNetworkIO();
    getDiskIO();
    initNvml();
}

SystemMonitor::~SystemMonitor() {
    if (nvml_handle && nvml_initialized) {
        auto pnvmlShutdown = (pfn_nvmlShutdown)dlsym(nvml_handle, "nvmlShutdown");
        if (pnvmlShutdown) pnvmlShutdown();
        dlclose(nvml_handle);
    }
}

void SystemMonitor::initNvml() {
    nvml_handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY);
    if (!nvml_handle) {
        nvml_handle = dlopen("libnvidia-ml.so", RTLD_LAZY);
    }

    if (nvml_handle) {
        auto pnvmlInit = (pfn_nvmlInit)dlsym(nvml_handle, "nvmlInit_v2");
        if (!pnvmlInit) pnvmlInit = (pfn_nvmlInit)dlsym(nvml_handle, "nvmlInit");

        if (pnvmlInit && pnvmlInit() == 0) {
            nvml_initialized = true;
        }
    }
}

GpuMetrics SystemMonitor::getNvidiaGpuMetrics() {
    GpuMetrics gpu;
    if (!nvml_initialized || !nvml_handle) return gpu;

    auto pGetHandle = (pfn_nvmlDeviceGetHandleByIndex)dlsym(nvml_handle, "nvmlDeviceGetHandleByIndex_v2");
    if (!pGetHandle) pGetHandle = (pfn_nvmlDeviceGetHandleByIndex)dlsym(nvml_handle, "nvmlDeviceGetHandleByIndex");

    auto pGetUtil = (pfn_nvmlDeviceGetUtilizationRates)dlsym(nvml_handle, "nvmlDeviceGetUtilizationRates");
    auto pGetMem = (pfn_nvmlDeviceGetMemoryInfo)dlsym(nvml_handle, "nvmlDeviceGetMemoryInfo");
    auto pGetTemp = (pfn_nvmlDeviceGetTemperature)dlsym(nvml_handle, "nvmlDeviceGetTemperature");
    auto pGetName = (pfn_nvmlDeviceGetName)dlsym(nvml_handle, "nvmlDeviceGetName");

    if (!pGetHandle || !pGetUtil || !pGetMem) return gpu;

    void* device_handle = nullptr;
    if (pGetHandle(0, &device_handle) == 0 && device_handle) {
        gpu.available = true;

        char name_buf[64] = {0};
        if (pGetName && pGetName(device_handle, name_buf, sizeof(name_buf)) == 0) {
            gpu.name = name_buf;
        } else {
            gpu.name = "NVIDIA GPU";
        }

        NvmlUtilization util;
        if (pGetUtil(device_handle, &util) == 0) {
            gpu.usage_pct = static_cast<float>(util.gpu);
        }

        NvmlMemory mem;
        if (pGetMem(device_handle, &mem) == 0) {
            gpu.vram_used_mb = static_cast<float>(mem.used / (1024 * 1024));
            gpu.vram_total_mb = static_cast<float>(mem.total / (1024 * 1024));
        }

        if (pGetTemp) {
            unsigned int temp = 0;
            if (pGetTemp(device_handle, 0, &temp) == 0) {
                gpu.temp_c = static_cast<float>(temp);
            }
        }
    }

    return gpu;
}

GpuMetrics SystemMonitor::getAmdIntelGpuMetrics() {
    GpuMetrics gpu;

    for (int i = 0; i < 4; ++i) {
        std::string base_path = "/sys/class/drm/card" + std::to_string(i) + "/device/";
        std::string busy_file = base_path + "gpu_busy_percent";

        if (fs::exists(busy_file)) {
            std::ifstream fbusy(busy_file);
            if (fbusy.is_open()) {
                fbusy >> gpu.usage_pct;
                gpu.available = true;
            }

            std::string vram_used_file = base_path + "mem_info_vram_used";
            std::string vram_total_file = base_path + "mem_info_vram_total";

            if (fs::exists(vram_used_file)) {
                std::ifstream fused(vram_used_file);
                uint64_t bytes_used = 0;
                if (fused >> bytes_used) {
                    gpu.vram_used_mb = static_cast<float>(bytes_used / (1024 * 1024));
                }
            }

            if (fs::exists(vram_total_file)) {
                std::ifstream ftot(vram_total_file);
                uint64_t bytes_tot = 0;
                if (ftot >> bytes_tot) {
                    gpu.vram_total_mb = static_cast<float>(bytes_tot / (1024 * 1024));
                }
            }

            // Lectura de temperatura del HWMon de la GPU
            std::string hwmon_dir = base_path + "hwmon/";
            if (fs::exists(hwmon_dir)) {
                for (const auto& entry : fs::directory_iterator(hwmon_dir)) {
                    std::string temp_file = entry.path().string() + "/temp1_input";
                    if (fs::exists(temp_file)) {
                        std::ifstream ftemp(temp_file);
                        float raw_temp = 0.0f;
                        if (ftemp >> raw_temp) {
                            gpu.temp_c = raw_temp / 1000.0f;
                        }
                        break;
                    }
                }
            }

            gpu.name = "AMD/Intel GPU";
            break;
        }
    }

    return gpu;
}

GpuMetrics SystemMonitor::getGpuMetrics() {
    if (nvml_initialized) {
        GpuMetrics nVidiaGpu = getNvidiaGpuMetrics();
        if (nVidiaGpu.available) return nVidiaGpu;
    }
    return getAmdIntelGpuMetrics();
}

float SystemMonitor::getCpuUsage() {
    std::ifstream file("/proc/stat");
    if (!file.is_open()) return 0.0f;

    std::string line;
    std::getline(file, line);
    std::istringstream ss(line);

    std::string cpu;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    ss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    uint64_t current_idle = idle + iowait;
    uint64_t current_total = user + nice + system + idle + iowait + irq + softirq + steal;

    uint64_t total_diff = current_total - last_total_time;
    uint64_t idle_diff = current_idle - last_idle_time;

    last_idle_time = current_idle;
    last_total_time = current_total;

    if (total_diff == 0) return 0.0f;
    return static_cast<float>(total_diff - idle_diff) / static_cast<float>(total_diff) * 100.0f;
}

float SystemMonitor::getRamUsageMB() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        uint64_t used_ram = info.totalram - info.freeram - info.bufferram;
        return static_cast<float>(used_ram * info.mem_unit) / (1024.0f * 1024.0f);
    }
    return 0.0f;
}

float SystemMonitor::getCpuTemperature() {
    std::string thermal_dir = "/sys/class/thermal/";
    if (!fs::exists(thermal_dir)) return 0.0f;

    for (const auto& entry : fs::directory_iterator(thermal_dir)) {
        std::string path = entry.path().string();
        if (path.find("thermal_zone") != std::string::npos) {
            std::ifstream type_file(path + "/type");
            std::string type;
            if (type_file >> type) {
                if (type.find("x86_pkg_temp") != std::string::npos || 
                    type.find("cpu_thermal") != std::string::npos ||
                    type.find("acpitz") != std::string::npos) {
                    
                    std::ifstream temp_file(path + "/temp");
                    float raw_temp = 0.0f;
                    if (temp_file >> raw_temp) {
                        return raw_temp / 1000.0f;
                    }
                }
            }
        }
    }
    return 0.0f;
}

NetworkMetrics SystemMonitor::getNetworkIO() {
    NetworkMetrics metrics;
    std::ifstream file("/proc/net/dev");
    if (!file.is_open()) return metrics;

    std::string line;
    uint64_t total_rx = 0, total_tx = 0;

    std::getline(file, line);
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        std::string iface;
        ss >> iface;

        if (iface.find("lo:") != std::string::npos) continue;

        uint64_t rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        uint64_t tx_bytes;

        if (ss >> rx_bytes >> rx_packets >> rx_errs >> rx_drop >> rx_fifo >> rx_frame >> rx_compressed >> rx_multicast >> tx_bytes) {
            total_rx += rx_bytes;
            total_tx += tx_bytes;
        }
    }

    auto now = std::chrono::steady_clock::now();
    double current_time = std::chrono::duration<double>(now.time_since_epoch()).count();
    double time_diff = current_time - last_net_time;

    if (last_net_time > 0.0 && time_diff > 0.0) {
        metrics.download_kbps = static_cast<float>((total_rx - last_net_rx) / 1024.0 / time_diff);
        metrics.upload_kbps = static_cast<float>((total_tx - last_net_tx) / 1024.0 / time_diff);
    }

    last_net_rx = total_rx;
    last_net_tx = total_tx;
    last_net_time = current_time;

    return metrics;
}

DiskMetrics SystemMonitor::getDiskIO() {
    DiskMetrics metrics;
    std::ifstream file("/proc/diskstats");
    if (!file.is_open()) return metrics;

    std::string line;
    uint64_t total_read_sectors = 0, total_write_sectors = 0;

    while (std::getline(file, line)) {
        std::istringstream ss(line);
        int major, minor;
        std::string dev_name;
        uint64_t reads, read_merges, read_sectors, read_ticks;
        uint64_t writes, write_merges, write_sectors;

        if (ss >> major >> minor >> dev_name >> reads >> read_merges >> read_sectors >> read_ticks >> writes >> write_merges >> write_sectors) {
            if (dev_name.find("loop") == 0 || dev_name.find("ram") == 0) continue;
            
            if (dev_name.find("sd") == 0 || dev_name.find("nvme") == 0 || dev_name.find("vd") == 0) {
                total_read_sectors += read_sectors;
                total_write_sectors += write_sectors;
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    double current_time = std::chrono::duration<double>(now.time_since_epoch()).count();
    double time_diff = current_time - last_disk_time;

    if (last_disk_time > 0.0 && time_diff > 0.0) {
        metrics.read_kbps = static_cast<float>((total_read_sectors - last_disk_read) * 512.0 / 1024.0 / time_diff);
        metrics.write_kbps = static_cast<float>((total_write_sectors - last_disk_write) * 512.0 / 1024.0 / time_diff);
    }

    last_disk_read = total_read_sectors;
    last_disk_write = total_write_sectors;
    last_disk_time = current_time;

    return metrics;
}

SystemInfo SystemMonitor::getStaticSystemInfo() {
    SystemInfo info;

    std::ifstream cpu_file("/proc/cpuinfo");
    std::string line;
    uint32_t cores = 0;

    while (std::getline(cpu_file, line)) {
        if (line.find("model name") != std::string::npos) {
            if (info.cpu_model.empty()) {
                info.cpu_model = line.substr(line.find(":") + 2);
            }
        } else if (line.find("vendor_id") != std::string::npos) {
            if (info.cpu_vendor.empty()) {
                info.cpu_vendor = line.substr(line.find(":") + 2);
            }
        } else if (line.find("cpu MHz") != std::string::npos) {
            if (info.cpu_freq_mhz.empty()) {
                info.cpu_freq_mhz = line.substr(line.find(":") + 2) + " MHz";
            }
        } else if (line.find("cache size") != std::string::npos) {
            if (info.cpu_cache_l3.empty()) {
                info.cpu_cache_l3 = line.substr(line.find(":") + 2);
            }
        } else if (line.find("processor") != std::string::npos) {
            cores++;
        }
    }
    info.cpu_cores = cores;

    struct sysinfo sys_info;
    if (sysinfo(&sys_info) == 0) {
        info.total_ram_mb = (sys_info.totalram * sys_info.mem_unit) / (1024 * 1024);
        info.total_swap_mb = (sys_info.totalswap * sys_info.mem_unit) / (1024 * 1024);
    }

    struct utsname os_info;
    if (uname(&os_info) == 0) {
        info.os_name = os_info.sysname;
        info.kernel_version = os_info.release;
        info.architecture = os_info.machine;
        info.hostname = os_info.nodename;
    }

    std::ifstream mounts("/proc/mounts");
    while (std::getline(mounts, line)) {
        std::istringstream ss(line);
        std::string device, mount_point, fstype;
        if (ss >> device >> mount_point >> fstype) {
            if (device.find("/dev/sd") == 0 || device.find("/dev/nvme") == 0) {
                struct statvfs stat;
                if (statvfs(mount_point.c_str(), &stat) == 0) {
                    DiskDriveInfo drive;
                    drive.mount_point = mount_point;
                    drive.total_gb = (stat.f_blocks * stat.f_frsize) / (1024 * 1024 * 1024);
                    drive.free_gb = (stat.f_bfree * stat.f_frsize) / (1024 * 1024 * 1024);
                    
                    bool exists = false;
                    for (const auto& d : info.drives) {
                        if (d.mount_point == drive.mount_point) {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists && drive.total_gb > 0) {
                        info.drives.push_back(drive);
                    }
                }
            }
        }
    }

    return info;
}

uint64_t SystemMonitor::getUptimeSeconds() {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
    return 0;
}