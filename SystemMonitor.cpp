#include "SystemMonitor.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
    #include <psapi.h>
#else
    #include <unistd.h>
    #include <sys/utsname.h>
    #include <sys/statvfs.h>
#endif

SystemMonitor::SystemMonitor() {
    getCpuUsage();
#ifndef _WIN32
    last_net_time = std::chrono::steady_clock::now();
    last_disk_time = std::chrono::steady_clock::now();
    getNetworkIO();
    getDiskIO();
#endif
}

unsigned int SystemMonitor::getCpuCores() {
    unsigned int cores = std::thread::hardware_concurrency();
    return (cores > 0) ? cores : 1;
}

uint64_t SystemMonitor::getUptimeSeconds() {
#ifdef _WIN32
    return GetTickCount64() / 1000;
#else
    std::ifstream file("/proc/uptime");
    double uptime = 0.0;
    if (file >> uptime) return static_cast<uint64_t>(uptime);
    return 0;
#endif
}

float SystemMonitor::getCpuTemperature() {
#ifndef _WIN32
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    if (file.is_open()) {
        float raw_temp;
        if (file >> raw_temp) return raw_temp / 1000.0f;
    }
#endif
    return 0.0f;
}

SystemInfo SystemMonitor::getStaticSystemInfo() {
    SystemInfo info;
    info.cpu_cores = getCpuCores();

#ifdef _WIN32
    info.os_name = "Microsoft Windows";
    
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char cpuName[256];
        DWORD bufferSize = sizeof(cpuName);
        if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, NULL, (LPBYTE)cpuName, &bufferSize) == ERROR_SUCCESS) {
            info.cpu_model = cpuName;
        }
        RegCloseKey(hKey);
    }

    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        info.total_ram_mb = memInfo.ullTotalPhys / (1024 * 1024);
        info.total_swap_mb = (memInfo.ullTotalPageFile - memInfo.ullTotalPhys) / (1024 * 1024);
    }
#else
    // OS & Kernel Info vía utsname
    struct utsname buffer;
    if (uname(&buffer) == 0) {
        info.os_name = buffer.sysname;
        info.kernel_version = buffer.release;
        info.hostname = buffer.nodename;
        info.architecture = buffer.machine;
    }

    // Parseo exhaustivo de /proc/cpuinfo
    std::ifstream cpuFile("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuFile, line)) {
        if (line.rfind("vendor_id", 0) == 0 && info.cpu_vendor == "Desconocido") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) info.cpu_vendor = line.substr(pos + 2);
        } else if (line.rfind("model name", 0) == 0 && info.cpu_model == "Desconocido") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) info.cpu_model = line.substr(pos + 2);
        } else if (line.rfind("cpu MHz", 0) == 0 && info.cpu_freq_mhz == "Desconocido") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) info.cpu_freq_mhz = line.substr(pos + 2) + " MHz";
        } else if (line.rfind("cache size", 0) == 0 && info.cpu_cache_l3 == "Desconocido") {
            size_t pos = line.find(':');
            if (pos != std::string::npos) info.cpu_cache_l3 = line.substr(pos + 2);
        }
    }

    // Parseo de /proc/meminfo para RAM y Swap
    std::ifstream memFile("/proc/meminfo");
    while (std::getline(memFile, line)) {
        if (line.rfind("MemTotal:", 0) == 0) {
            std::stringstream ss(line.substr(9));
            uint64_t totalKb;
            ss >> totalKb;
            info.total_ram_mb = totalKb / 1024;
        } else if (line.rfind("SwapTotal:", 0) == 0) {
            std::stringstream ss(line.substr(10));
            uint64_t swapKb;
            ss >> swapKb;
            info.total_swap_mb = swapKb / 1024;
        }
    }

    // Información de disco raíz usando statvfs
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        StorageDrive rootDrive;
        rootDrive.mount_point = "/ (Raiz)";
        rootDrive.total_gb = (stat.f_blocks * stat.f_frsize) / (1024 * 1024 * 1024);
        rootDrive.free_gb = (stat.f_bavail * stat.f_frsize) / (1024 * 1024 * 1024);
        info.drives.push_back(rootDrive);
    }
#endif

    return info;
}

uint64_t SystemMonitor::getRamUsageMB() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
        return static_cast<uint64_t>(physMemUsed / (1024 * 1024));
    }
    return 0;
#else
    std::ifstream file("/proc/meminfo");
    std::string line;
    uint64_t totalMem = 0, availMem = 0;

    while (std::getline(file, line)) {
        if (line.compare(0, 9, "MemTotal:") == 0) {
            std::stringstream ss(line.substr(9));
            ss >> totalMem;
        } else if (line.compare(0, 13, "MemAvailable:") == 0) {
            std::stringstream ss(line.substr(13));
            ss >> availMem;
        }
    }
    return (totalMem - availMem) / 1024;
#endif
}

uint64_t SystemMonitor::fileTimeToUint64(const void* ft_ptr) {
#ifdef _WIN32
    const FILETIME* ft = static_cast<const FILETIME*>(ft_ptr);
    return (static_cast<uint64_t>(ft->dwHighDateTime) << 32) | ft->dwLowDateTime;
#else
    return 0;
#endif
}

float SystemMonitor::getCpuUsage() {
#ifdef _WIN32
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) return 0.0f;

    uint64_t idle = fileTimeToUint64(&idleTime);
    uint64_t kernel = fileTimeToUint64(&kernelTime);
    uint64_t user = fileTimeToUint64(&userTime);

    uint64_t idle_diff = idle - prev_idle_time;
    uint64_t kernel_diff = kernel - prev_kernel_time;
    uint64_t user_diff = user - prev_user_time;

    prev_idle_time = idle;
    prev_kernel_time = kernel;
    prev_user_time = user;

    uint64_t total = kernel_diff + user_diff;
    if (total == 0) return 0.0f;

    return static_cast<float>(total - idle_diff) * 100.0f / static_cast<float>(total);
#else
    std::ifstream file("/proc/stat");
    std::string cpuLabel;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;

    file >> cpuLabel >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    uint64_t prevIdle = prev_idle + prev_iowait;
    uint64_t idleCurrent = idle + iowait;

    uint64_t prevNonIdle = prev_user + prev_nice + prev_system + prev_irq + prev_softirq + prev_steal;
    uint64_t nonIdleCurrent = user + nice + system + irq + softirq + steal;

    uint64_t prevTotal = prevIdle + prevNonIdle;
    uint64_t totalCurrent = idleCurrent + nonIdleCurrent;

    uint64_t totalDiff = totalCurrent - prevTotal;
    uint64_t idleDiff = idleCurrent - prevIdle;

    prev_user = user; prev_nice = nice; prev_system = system; prev_idle = idle;
    prev_iowait = iowait; prev_irq = irq; prev_softirq = softirq; prev_steal = steal;

    if (totalDiff == 0) return 0.0f;
    return static_cast<float>(totalDiff - idleDiff) * 100.0f / static_cast<float>(totalDiff);
#endif
}

NetworkMetrics SystemMonitor::getNetworkIO() {
    NetworkMetrics metrics;
#ifndef _WIN32
    std::ifstream file("/proc/net/dev");
    std::string line;
    uint64_t total_rx = 0, total_tx = 0;

    std::getline(file, line);
    std::getline(file, line);

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string iface;
        uint64_t rx, tx, dummy;

        ss >> iface >> rx;
        if (iface.find("lo:") != std::string::npos) continue;

        for (int i = 0; i < 7; ++i) ss >> dummy;
        ss >> tx;

        total_rx += rx;
        total_tx += tx;
    }

    auto now = std::chrono::steady_clock::now();
    float elapsed_sec = std::chrono::duration<float>(now - last_net_time).count();
    
    if (elapsed_sec > 0.0f && prev_rx_bytes > 0) {
        metrics.download_kbps = static_cast<float>(total_rx - prev_rx_bytes) / 1024.0f / elapsed_sec;
        metrics.upload_kbps = static_cast<float>(total_tx - prev_tx_bytes) / 1024.0f / elapsed_sec;
    }

    prev_rx_bytes = total_rx;
    prev_tx_bytes = total_tx;
    last_net_time = now;
#endif
    return metrics;
}

DiskMetrics SystemMonitor::getDiskIO() {
    DiskMetrics metrics;
#ifndef _WIN32
    std::ifstream file("/proc/diskstats");
    std::string line;
    uint64_t total_reads_sectors = 0, total_writes_sectors = 0;

    while (std::getline(file, line)) {
        std::stringstream ss(line);
        int major, minor;
        std::string dev_name;
        uint64_t r_completed, r_merged, r_sectors, r_time;
        uint64_t w_completed, w_merged, w_sectors, w_time;

        if (ss >> major >> minor >> dev_name >> r_completed >> r_merged >> r_sectors >> r_time
               >> w_completed >> w_merged >> w_sectors >> w_time) {
            
            if (dev_name.find("loop") == std::string::npos && dev_name.find("ram") == std::string::npos) {
                total_reads_sectors += r_sectors;
                total_writes_sectors += w_sectors;
            }
        }
    }

    auto now = std::chrono::steady_clock::now();
    float elapsed_sec = std::chrono::duration<float>(now - last_disk_time).count();

    if (elapsed_sec > 0.0f && prev_read_bytes > 0) {
        uint64_t read_bytes = total_reads_sectors * 512;
        uint64_t write_bytes = total_writes_sectors * 512;

        metrics.read_kbps = static_cast<float>(read_bytes - prev_read_bytes) / 1024.0f / elapsed_sec;
        metrics.write_kbps = static_cast<float>(write_bytes - prev_write_bytes) / 1024.0f / elapsed_sec;
    }

    prev_read_bytes = total_reads_sectors * 512;
    prev_write_bytes = total_writes_sectors * 512;
    last_disk_time = now;
#endif
    return metrics;
}