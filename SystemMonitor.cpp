#include "SystemMonitor.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <dxgi1_4.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "dxgi.lib")

static void* LoadNativeLib(const char* name) {
    return static_cast<void*>(LoadLibraryA(name));
}
static void* GetNativeSym(void* handle, const char* sym) {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), sym));
}
static void FreeNativeLib(void* handle) {
    if (handle) FreeLibrary(static_cast<HMODULE>(handle));
}
#else
#include <unistd.h>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <dlfcn.h>

static void* LoadNativeLib(const char* name) {
    return dlopen(name, RTLD_LAZY);
}
static void* GetNativeSym(void* handle, const char* sym) {
    return dlsym(handle, sym);
}
static void FreeNativeLib(void* handle) {
    if (handle) dlclose(handle);
}
#endif

namespace fs = std::filesystem;

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
        auto pnvmlShutdown = reinterpret_cast<pfn_nvmlShutdown>(GetNativeSym(nvml_handle, "nvmlShutdown"));
        if (pnvmlShutdown) pnvmlShutdown();
        FreeNativeLib(nvml_handle);
    }
}

void SystemMonitor::initNvml() {
#ifdef _WIN32
    nvml_handle = LoadNativeLib("nvml.dll");
    if (!nvml_handle) {
        nvml_handle = LoadNativeLib("C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll");
    }
#else
    nvml_handle = LoadNativeLib("libnvidia-ml.so.1");
    if (!nvml_handle) {
        nvml_handle = LoadNativeLib("libnvidia-ml.so");
    }
#endif

    if (nvml_handle) {
        auto pnvmlInit = reinterpret_cast<pfn_nvmlInit>(GetNativeSym(nvml_handle, "nvmlInit_v2"));
        if (!pnvmlInit) pnvmlInit = reinterpret_cast<pfn_nvmlInit>(GetNativeSym(nvml_handle, "nvmlInit"));

        if (pnvmlInit && pnvmlInit() == 0) {
            nvml_initialized = true;
        }
    }
}

GpuMetrics SystemMonitor::getNvidiaGpuMetrics() {
    GpuMetrics gpu;
    if (!nvml_initialized || !nvml_handle) return gpu;

    auto pGetHandle = reinterpret_cast<pfn_nvmlDeviceGetHandleByIndex>(GetNativeSym(nvml_handle, "nvmlDeviceGetHandleByIndex_v2"));
    if (!pGetHandle) pGetHandle = reinterpret_cast<pfn_nvmlDeviceGetHandleByIndex>(GetNativeSym(nvml_handle, "nvmlDeviceGetHandleByIndex"));

    auto pGetUtil = reinterpret_cast<pfn_nvmlDeviceGetUtilizationRates>(GetNativeSym(nvml_handle, "nvmlDeviceGetUtilizationRates"));
    auto pGetMem = reinterpret_cast<pfn_nvmlDeviceGetMemoryInfo>(GetNativeSym(nvml_handle, "nvmlDeviceGetMemoryInfo"));
    auto pGetTemp = reinterpret_cast<pfn_nvmlDeviceGetTemperature>(GetNativeSym(nvml_handle, "nvmlDeviceGetTemperature"));
    auto pGetName = reinterpret_cast<pfn_nvmlDeviceGetName>(GetNativeSym(nvml_handle, "nvmlDeviceGetName"));

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

GpuMetrics SystemMonitor::getWindowsDxgiGpuMetrics() {
    GpuMetrics gpu;
#ifdef _WIN32
    typedef HRESULT (WINAPI *pfn_CreateDXGIFactory1)(REFIID, void**);
    
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (!hDxgi) return gpu;

    auto pCreateFactory = reinterpret_cast<pfn_CreateDXGIFactory1>(GetProcAddress(hDxgi, "CreateDXGIFactory1"));
    if (!pCreateFactory) {
        FreeLibrary(hDxgi);
        return gpu;
    }

    static const GUID IID_IDXGIFactory4_local = { 0x1bc6ea98, 0x9fb0, 0x47c6, { 0x9c, 0x51, 0x64, 0x64, 0x07, 0x00, 0x08, 0x90 } };
    static const GUID IID_IDXGIAdapter3_local = { 0x645967a4, 0xaa28, 0x4d6e, { 0x80, 0x6e, 0x10, 0x24, 0x5b, 0x08, 0x9c, 0x5f } };

    IDXGIFactory4* pFactory = nullptr;
    if (SUCCEEDED(pCreateFactory(IID_IDXGIFactory4_local, reinterpret_cast<void**>(&pFactory))) && pFactory) {
        IDXGIAdapter1* pAdapter = nullptr;
        if (SUCCEEDED(pFactory->EnumAdapters1(0, &pAdapter)) && pAdapter) {
            DXGI_ADAPTER_DESC1 desc;
            if (SUCCEEDED(pAdapter->GetDesc1(&desc))) {
                char nameBuf[128];
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nameBuf, sizeof(nameBuf), NULL, NULL);
                gpu.name = nameBuf;
                gpu.vram_total_mb = static_cast<float>(desc.DedicatedVideoMemory / (1024 * 1024));

                IDXGIAdapter3* pAdapter3 = nullptr;
                if (SUCCEEDED(pAdapter->QueryInterface(IID_IDXGIAdapter3_local, reinterpret_cast<void**>(&pAdapter3))) && pAdapter3) {
                    DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
                    if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                        gpu.vram_used_mb = static_cast<float>(memInfo.CurrentUsage / (1024 * 1024));
                        if (gpu.vram_total_mb == 0.0f) {
                            gpu.vram_total_mb = static_cast<float>(memInfo.Budget / (1024 * 1024));
                        }
                    }
                    pAdapter3->Release();
                }
                gpu.available = true;
            }
            pAdapter->Release();
        }
        pFactory->Release();
    }
    FreeLibrary(hDxgi);
#endif
    return gpu;
}

GpuMetrics SystemMonitor::getAmdIntelGpuMetrics() {
    GpuMetrics gpu;
#ifndef _WIN32
    for (int i = 0; i < 4; ++i) {
        std::string base_path = "/sys/class/drm/card" + std::to_string(i) + "/device/";
        if (!fs::exists(base_path)) continue;

        std::string vendor_file = base_path + "vendor";
        std::string vendor_id = "";
        if (fs::exists(vendor_file)) {
            std::ifstream fv(vendor_file);
            fv >> vendor_id;
        }

        std::string busy_file = base_path + "gpu_busy_percent";
        if (fs::exists(busy_file)) {
            std::ifstream fbusy(busy_file);
            if (fbusy >> gpu.usage_pct) {
                gpu.available = true;
                gpu.name = "AMD GPU";
            }

            std::string vram_used_file = base_path + "mem_info_vram_used";
            std::string vram_total_file = base_path + "mem_info_vram_total";

            if (fs::exists(vram_used_file)) {
                std::ifstream fused(vram_used_file);
                uint64_t bytes_used = 0;
                if (fused >> bytes_used) gpu.vram_used_mb = static_cast<float>(bytes_used / (1024 * 1024));
            }
            if (fs::exists(vram_total_file)) {
                std::ifstream ftot(vram_total_file);
                uint64_t bytes_tot = 0;
                if (ftot >> bytes_tot) gpu.vram_total_mb = static_cast<float>(bytes_tot / (1024 * 1024));
            }

            std::string hwmon_dir = base_path + "hwmon/";
            if (fs::exists(hwmon_dir)) {
                for (const auto& entry : fs::directory_iterator(hwmon_dir)) {
                    std::string temp_file = entry.path().string() + "/temp1_input";
                    if (fs::exists(temp_file)) {
                        std::ifstream ftemp(temp_file);
                        float raw_temp = 0.0f;
                        if (ftemp >> raw_temp) gpu.temp_c = raw_temp / 1000.0f;
                        break;
                    }
                }
            }
            break;
        }

        std::string gt_act_freq = "/sys/class/drm/card" + std::to_string(i) + "/gt/gt0/gt_act_freq_mhz";
        std::string rps_act_freq = "/sys/class/drm/card" + std::to_string(i) + "/gt/gt0/rps_act_freq_mhz";
        std::string rps_max_freq = "/sys/class/drm/card" + std::to_string(i) + "/gt/gt0/rps_max_freq_mhz";
        std::string gt_max_freq = "/sys/class/drm/card" + std::to_string(i) + "/gt/gt0/gt_max_freq_mhz";

        std::string act_path = fs::exists(gt_act_freq) ? gt_act_freq : (fs::exists(rps_act_freq) ? rps_act_freq : "");
        std::string max_path = fs::exists(rps_max_freq) ? rps_max_freq : (fs::exists(gt_max_freq) ? gt_max_freq : "");

        if (!act_path.empty() && !max_path.empty()) {
            std::ifstream fact(act_path), fmax(max_path);
            float act = 0.0f, max_f = 0.0f;
            if (fact >> act && fmax >> max_f && max_f > 0.0f) {
                gpu.usage_pct = std::min(100.0f, (act / max_f) * 100.0f);
                gpu.available = true;
                gpu.name = "Intel Graphics";
            }
        } else if (vendor_id == "0x8086") {
            gpu.available = true;
            gpu.name = "Intel iGPU";
            gpu.usage_pct = 0.0f;
        }

        std::string intel_mem_used = "/sys/class/drm/card" + std::to_string(i) + "/memory/used";
        if (fs::exists(intel_mem_used)) {
            std::ifstream fmem(intel_mem_used);
            float mem_mb = 0.0f;
            if (fmem >> mem_mb) gpu.vram_used_mb = mem_mb;
        }

        std::string intel_temp = "/sys/class/drm/card" + std::to_string(i) + "/thermal_zone0/temp";
        if (fs::exists(intel_temp)) {
            std::ifstream ftemp(intel_temp);
            float raw_temp = 0.0f;
            if (ftemp >> raw_temp) gpu.temp_c = raw_temp > 1000.0f ? raw_temp / 1000.0f : raw_temp;
        }

        if (gpu.available) break;
    }
#endif
    return gpu;
}

GpuMetrics SystemMonitor::getGpuMetrics() {
    if (nvml_initialized) {
        GpuMetrics nVidiaGpu = getNvidiaGpuMetrics();
        if (nVidiaGpu.available) return nVidiaGpu;
    }
#ifdef _WIN32
    return getWindowsDxgiGpuMetrics();
#else
    return getAmdIntelGpuMetrics();
#endif
}
float SystemMonitor::getCpuTemperature() {
#ifdef _WIN32
    float temp = 0.0f;
    HRESULT hres = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool co_initialized = SUCCEEDED(hres) || hres == S_OK || hres == S_FALSE;

    IWbemLocator *pLoc = NULL;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<LPVOID*>(&pLoc));
    if (SUCCEEDED(hr) && pLoc) {
        
        // --- FALLBACK 1: LibreHardwareMonitor WMI ---
        IWbemServices *pSvc = NULL;
        hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\LibreHardwareMonitor"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
        if (SUCCEEDED(hr) && pSvc) {
            CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                              RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

            IEnumWbemClassObject* pEnumerator = NULL;
            hr = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT Value FROM Sensor WHERE SensorType='Temperature' AND (Name LIKE '%CPU Package%' OR Name LIKE '%CPU Core%')"),
                WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
            if (SUCCEEDED(hr) && pEnumerator) {
                IWbemClassObject *pclsObj = NULL;
                ULONG uReturn = 0;
                if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                    VARIANT vtProp;
                    if (SUCCEEDED(pclsObj->Get(L"Value", 0, &vtProp, 0, 0)) && vtProp.vt == VT_R4) {
                        temp = vtProp.fltVal;
                        VariantClear(&vtProp);
                    }
                    pclsObj->Release();
                }
                pEnumerator->Release();
            }
            pSvc->Release();
        }

        // --- FALLBACK 2: OpenHardwareMonitor WMI ---
        if (temp <= 0.0f) {
            pSvc = NULL;
            hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\OpenHardwareMonitor"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (SUCCEEDED(hr) && pSvc) {
                CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

                IEnumWbemClassObject* pEnumerator = NULL;
                hr = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT Value FROM Sensor WHERE SensorType='Temperature' AND (Name LIKE '%CPU Package%' OR Name LIKE '%CPU Core%')"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
                if (SUCCEEDED(hr) && pEnumerator) {
                    IWbemClassObject *pclsObj = NULL;
                    ULONG uReturn = 0;
                    if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                        VARIANT vtProp;
                        if (SUCCEEDED(pclsObj->Get(L"Value", 0, &vtProp, 0, 0)) && vtProp.vt == VT_R4) {
                            temp = vtProp.fltVal;
                            VariantClear(&vtProp);
                        }
                        pclsObj->Release();
                    }
                    pEnumerator->Release();
                }
                pSvc->Release();
            }
        }

        // --- FALLBACK 3: WMI ACPI Nativo (Portátiles / Placas compatibles) ---
        if (temp <= 0.0f) {
            pSvc = NULL;
            hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
            if (SUCCEEDED(hr) && pSvc) {
                CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                                  RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

                IEnumWbemClassObject* pEnumerator = NULL;
                hr = pSvc->ExecQuery(_bstr_t("WQL"), _bstr_t("SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
                    WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
                if (SUCCEEDED(hr) && pEnumerator) {
                    IWbemClassObject *pclsObj = NULL;
                    ULONG uReturn = 0;
                    if (pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn) == S_OK && uReturn > 0) {
                        VARIANT vtProp;
                        if (SUCCEEDED(pclsObj->Get(L"CurrentTemperature", 0, &vtProp, 0, 0))) {
                            float decikelvin = static_cast<float>(vtProp.ulVal);
                            temp = (decikelvin - 2732.0f) / 10.0f;
                            VariantClear(&vtProp);
                        }
                        pclsObj->Release();
                    }
                    pEnumerator->Release();
                }
                pSvc->Release();
            }
        }

        pLoc->Release();
    }
    if (co_initialized) {
        CoUninitialize();
    }
    return (temp > 0.0f && temp < 125.0f) ? temp : 0.0f;
#else
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
#endif
}

float SystemMonitor::getCpuUsage() {
#ifdef _WIN32
    static FILETIME prev_idle, prev_kernel, prev_user;
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULARGE_INTEGER i, k, u, pi, pk, pu;
        i.LowPart = idle.dwLowDateTime; i.HighPart = idle.dwHighDateTime;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        pi.LowPart = prev_idle.dwLowDateTime; pi.HighPart = prev_idle.dwHighDateTime;
        pk.LowPart = prev_kernel.dwLowDateTime; pk.HighPart = prev_kernel.dwHighDateTime;
        pu.LowPart = prev_user.dwLowDateTime; pu.HighPart = prev_user.dwHighDateTime;

        ULONGLONG usr_diff = u.QuadPart - pu.QuadPart;
        ULONGLONG ker_diff = k.QuadPart - pk.QuadPart;
        ULONGLONG idl_diff = i.QuadPart - pi.QuadPart;
        ULONGLONG total_diff = usr_diff + ker_diff;

        prev_idle = idle; prev_kernel = kernel; prev_user = user;

        if (total_diff > 0) {
            return static_cast<float>((total_diff - idl_diff) * 100.0 / total_diff);
        }
    }
    return 0.0f;
#else
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
#endif
}

float SystemMonitor::getRamUsageMB() {
#ifdef _WIN32
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memInfo);
    DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
    return static_cast<float>(physMemUsed / (1024 * 1024));
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        uint64_t used_ram = info.totalram - info.freeram - info.bufferram;
        return static_cast<float>(used_ram * info.mem_unit) / (1024.0f * 1024.0f);
    }
    return 0.0f;
#endif
}

NetworkMetrics SystemMonitor::getNetworkIO() {
    NetworkMetrics metrics;
#ifdef _WIN32
    MIB_IFTABLE* pTable = NULL;
    DWORD dwSize = 0;
    if (GetIfTable(NULL, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        pTable = (MIB_IFTABLE*)malloc(dwSize);
        if (pTable && GetIfTable(pTable, &dwSize, FALSE) == NO_ERROR) {
            uint64_t total_rx = 0, total_tx = 0;
            for (DWORD i = 0; i < pTable->dwNumEntries; i++) {
                MIB_IFROW* row = &pTable->table[i];
                if (row->dwType != MIB_IF_TYPE_LOOPBACK) {
                    total_rx += row->dwInOctets;
                    total_tx += row->dwOutOctets;
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
        }
        if (pTable) free(pTable);
    }
#else
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
#endif
    return metrics;
}

DiskMetrics SystemMonitor::getDiskIO() {
    DiskMetrics metrics;
#ifndef _WIN32
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
#endif
    return metrics;
}

SystemInfo SystemMonitor::getStaticSystemInfo() {
    SystemInfo info;
#ifdef _WIN32
    info.os_name = "Windows";
    info.architecture = "x86_64";
    
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        info.total_ram_mb = memInfo.ullTotalPhys / (1024 * 1024);
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    info.cpu_cores = sysInfo.dwNumberOfProcessors;
    info.cpu_model = "Intel/AMD Processor";
#else
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
#endif
    return info;
}

uint64_t SystemMonitor::getUptimeSeconds() {
#ifdef _WIN32
    return GetTickCount64() / 1000;
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.uptime;
    }
    return 0;
#endif
}