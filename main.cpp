#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif

#if defined(_MSC_VER)
#pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif

#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "SystemMonitor.hpp"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

struct TelemetrySample {
    double timestamp_sec;
    float fps;
    float frametime_ms;
    float cpu_usage;
    float cpu_temp;
    float gpu_usage;
    float gpu_temp;
    float vram_mb;
    float ram_mb;
    float net_kbps;
    float disk_kbps;
};

std::string formatRate(float kbps) {
    char buffer[32];
    if (kbps >= 1024.0f) {
        snprintf(buffer, sizeof(buffer), "%.2f MB/s", kbps / 1024.0f);
    } else {
        snprintf(buffer, sizeof(buffer), "%.1f KB/s", kbps);
    }
    return std::string(buffer);
}

std::string formatUptime(uint64_t seconds) {
    uint64_t hrs = seconds / 3600;
    uint64_t mins = (seconds % 3600) / 60;
    uint64_t secs = seconds % 60;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%llu h %llu m %llu s", hrs, mins, secs);
    return std::string(buffer);
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void RenderKpiCard(const char* title, const char* value, const char* status, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Border, color);
    ImGui::BeginChild(title, ImVec2(0, 115), true, ImGuiWindowFlags_NoScrollbar);
    
    ImGui::TextColored(color, "%s", title);
    ImGui::Spacing();
    ImGui::TextUnformatted(value);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", status);
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void LoadCrispSystemFont(ImGuiIO& io, float font_size) {
    ImFontConfig font_config;
    font_config.OversampleH = 4;
    font_config.OversampleV = 4;
    font_config.PixelSnapH = true;

    const std::vector<std::string> font_paths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf"
    };

    bool font_loaded = false;
    for (const auto& path : font_paths) {
        std::ifstream file(path);
        if (file.good()) {
            io.Fonts->AddFontFromFileTTF(path.c_str(), font_size, &font_config);
            font_loaded = true;
            break;
        }
    }

    if (!font_loaded) {
        io.Fonts->AddFontDefault(&font_config);
    }
}

float CalculateLowFPS(const std::vector<float>& ft_buffer, float percentile) {
    if (ft_buffer.empty()) return 0.0f;

    std::vector<float> sorted_ft = ft_buffer;
    std::sort(sorted_ft.rbegin(), sorted_ft.rend());

    size_t count = std::max<size_t>(1, static_cast<size_t>(sorted_ft.size() * percentile));
    if (count > sorted_ft.size()) count = sorted_ft.size();

    double sum_worst_ms = std::accumulate(sorted_ft.begin(), sorted_ft.begin() + count, 0.0);
    double avg_worst_ms = sum_worst_ms / count;

    return (avg_worst_ms > 0.0) ? static_cast<float>(1000.0 / avg_worst_ms) : 0.0f;
}

void CenterWindow(GLFWwindow* window, int target_w, int target_h) {
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (primary) {
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        if (mode) {
            int w = std::min(target_w, mode->width - 80);
            int h = std::min(target_h, mode->height - 80);
            int xpos = std::max(0, (mode->width - w) / 2);
            int ypos = std::max(0, (mode->height - h) / 2);

            glfwSetWindowSize(window, w, h);
            glfwSetWindowPos(window, xpos, ypos);
            return;
        }
    }
    glfwSetWindowSize(window, target_w, target_h);
    glfwSetWindowPos(window, 100, 100);
}

std::string ExportBenchmarkToCSV(const std::vector<TelemetrySample>& samples) {
    if (samples.empty()) return "";

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "benchmark_" << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S") << ".csv";
    std::string filename = ss.str();

    std::ofstream file(filename);
    if (!file.is_open()) return "";

    file << "Time_Sec,FPS,Frametime_ms,CPU_Usage_Pct,CPU_Temp_C,GPU_Usage_Pct,GPU_Temp_C,VRAM_Used_MB,RAM_Used_MB,Net_DL_KBps,Disk_Read_KBps\n";
    for (const auto& s : samples) {
        file << std::fixed << std::setprecision(3)
             << s.timestamp_sec << ","
             << std::setprecision(1)
             << s.fps << ","
             << std::setprecision(2)
             << s.frametime_ms << ","
             << std::setprecision(1)
             << s.cpu_usage << ","
             << s.cpu_temp << ","
             << s.gpu_usage << ","
             << s.gpu_temp << ","
             << s.vram_mb << ","
             << s.ram_mb << ","
             << s.net_kbps << ","
             << s.disk_kbps << "\n";
    }
    file.close();
    return filename;
}

int main() {
    if (!glfwInit()) return -1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1440, 900, "GamePerfTrace - Dashboard Pro", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    CenterWindow(window, 1440, 900);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    int win_w, win_h, fb_w, fb_h;
    glfwGetWindowSize(window, &win_w, &win_h);
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    float dpi_scale = (win_w > 0) ? (static_cast<float>(fb_w) / static_cast<float>(win_w)) : 1.0f;

    float target_font_size = 20.0f * (dpi_scale > 1.0f ? dpi_scale : 1.0f);
    LoadCrispSystemFont(io, target_font_size);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(dpi_scale > 1.0f ? dpi_scale : 1.2f);
    style.WindowRounding = 0.0f;
    style.FrameRounding = 6.0f;
    style.ItemSpacing = ImVec2(12, 12);
    style.FramePadding = ImVec2(10, 8);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    SystemMonitor monitor;
    SystemInfo specs = monitor.getStaticSystemInfo();

    const char* vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    specs.gpu_vendor = vendor ? vendor : "Desconocido";
    specs.gpu_renderer = renderer ? renderer : "Desconocido";
    specs.gpu_version = version ? version : "Desconocido";
    specs.gpu_glsl_version = glsl ? glsl : "Desconocido";

    const int MAX_HARDWARE_SAMPLES = 100;
    std::vector<float> cpu_history(MAX_HARDWARE_SAMPLES, 0.0f);
    std::vector<float> gpu_history(MAX_HARDWARE_SAMPLES, 0.0f);
    std::vector<float> ram_history(MAX_HARDWARE_SAMPLES, 0.0f);
    std::vector<float> net_dl_history(MAX_HARDWARE_SAMPLES, 0.0f);
    std::vector<float> disk_read_history(MAX_HARDWARE_SAMPLES, 0.0f);

    const int MAX_FRAME_SAMPLES = 300;
    std::vector<float> frametime_history;
    frametime_history.reserve(MAX_FRAME_SAMPLES);

    double last_hardware_update = glfwGetTime();
    double last_frame_time = glfwGetTime();
    
    float current_fps = 0.0f;
    float current_frametime_ms = 0.0f;
    float fps_1percent_low = 0.0f;
    float current_temp = 0.0f;
    GpuMetrics current_gpu;

    bool is_hud_mode = false;
    bool f12_pressed_prev = false;

    // Estado del HUD: Tamaño ajustado (340x135) para contener las 4 filas holgadamente
    int hud_win_x = 25;
    int hud_win_y = 25;
    int hud_win_w = 340;
    int hud_win_h = 135;

    // Arrastre dinámico
    bool is_dragging_hud = false;
    double drag_start_offset_x = 0.0;
    double drag_start_offset_y = 0.0;

    // Benchmark / Exportación CSV
    bool is_benchmarking = false;
    bool f11_pressed_prev = false;
    double benchmark_start_time = 0.0;
    std::vector<TelemetrySample> benchmark_data;

    bool show_benchmark_result = false;
    std::string last_csv_filename = "";
    double last_bench_duration = 0.0;
    float last_bench_avg_fps = 0.0f;
    float last_bench_1low_fps = 0.0f;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double now = glfwGetTime();

        // 1. CONMUTACIÓN MODO HUD CON F12
        bool f12_pressed_now = (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS);
        if (f12_pressed_now && !f12_pressed_prev) {
            is_hud_mode = !is_hud_mode;
            
            if (is_hud_mode) {
                glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
                glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
                glfwPollEvents();
                glfwSetWindowSize(window, hud_win_w, hud_win_h);
                glfwSetWindowPos(window, hud_win_x, hud_win_y);
            } else {
                glfwGetWindowPos(window, &hud_win_x, &hud_win_y);
                glfwGetWindowSize(window, &hud_win_w, &hud_win_h);

                glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
                glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
                glfwPollEvents();
                CenterWindow(window, 1440, 900);
            }
        }
        f12_pressed_prev = f12_pressed_now;

        // 2. ACTIVACIÓN / DESACTIVACIÓN BENCHMARK CON F11
        bool f11_pressed_now = (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS);
        if (f11_pressed_now && !f11_pressed_prev) {
            is_benchmarking = !is_benchmarking;

            if (is_benchmarking) {
                benchmark_data.clear();
                benchmark_start_time = now;
                show_benchmark_result = false;
            } else {
                last_bench_duration = now - benchmark_start_time;
                if (!benchmark_data.empty()) {
                    last_csv_filename = ExportBenchmarkToCSV(benchmark_data);

                    double sum_fps = 0.0;
                    std::vector<float> bench_fts;
                    bench_fts.reserve(benchmark_data.size());

                    for (const auto& sample : benchmark_data) {
                        sum_fps += sample.fps;
                        bench_fts.push_back(sample.frametime_ms);
                    }

                    last_bench_avg_fps = static_cast<float>(sum_fps / benchmark_data.size());
                    last_bench_1low_fps = CalculateLowFPS(bench_fts, 0.01f);
                    show_benchmark_result = true;
                }
            }
        }
        f11_pressed_prev = f11_pressed_now;

        // Guardia de seguridad para Linux (Dashboard)
        if (!is_hud_mode) {
            int current_w, current_h;
            glfwGetWindowSize(window, &current_w, &current_h);
            if (current_w < 500) {
                CenterWindow(window, 1440, 900);
            }
        }

        // Medición de Frametime y FPS
        double frame_delta = now - last_frame_time;
        last_frame_time = now;
        
        if (frame_delta > 0.0) {
            current_frametime_ms = static_cast<float>(frame_delta * 1000.0);
            current_fps = static_cast<float>(1.0 / frame_delta);

            frametime_history.push_back(current_frametime_ms);
            if (frametime_history.size() > MAX_FRAME_SAMPLES) {
                frametime_history.erase(frametime_history.begin());
            }

            fps_1percent_low = CalculateLowFPS(frametime_history, 0.01f);

            if (is_benchmarking) {
                TelemetrySample sample;
                sample.timestamp_sec = now - benchmark_start_time;
                sample.fps = current_fps;
                sample.frametime_ms = current_frametime_ms;
                sample.cpu_usage = cpu_history.empty() ? 0.0f : cpu_history.back();
                sample.cpu_temp = current_temp;
                sample.gpu_usage = current_gpu.usage_pct;
                sample.gpu_temp = current_gpu.temp_c;
                sample.vram_mb = current_gpu.vram_used_mb;
                sample.ram_mb = ram_history.empty() ? 0.0f : ram_history.back();
                sample.net_kbps = net_dl_history.empty() ? 0.0f : net_dl_history.back();
                sample.disk_kbps = disk_read_history.empty() ? 0.0f : disk_read_history.back();

                benchmark_data.push_back(sample);
            }
        }

        // Muestreo de Hardware cada 0.5s
        if (now - last_hardware_update >= 0.5) {
            float cpu = monitor.getCpuUsage();
            float ram = static_cast<float>(monitor.getRamUsageMB());
            current_temp = monitor.getCpuTemperature();
            current_gpu = monitor.getGpuMetrics();
            NetworkMetrics net = monitor.getNetworkIO();
            DiskMetrics disk = monitor.getDiskIO();

            std::rotate(cpu_history.begin(), cpu_history.begin() + 1, cpu_history.end());
            cpu_history.back() = cpu;

            std::rotate(gpu_history.begin(), gpu_history.begin() + 1, gpu_history.end());
            gpu_history.back() = current_gpu.usage_pct;

            std::rotate(ram_history.begin(), ram_history.begin() + 1, ram_history.end());
            ram_history.back() = ram;

            std::rotate(net_dl_history.begin(), net_dl_history.begin() + 1, net_dl_history.end());
            net_dl_history.back() = net.download_kbps;

            std::rotate(disk_read_history.begin(), disk_read_history.begin() + 1, disk_read_history.end());
            disk_read_history.back() = disk.read_kbps;

            last_hardware_update = now;
        }

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (is_hud_mode) {
            // --- MODO 1: MICRO-HUD OVERLAY INTERACTIVO ---
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSizeConstraints(ImVec2(280, 120), ImVec2(700, 400));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            
            if (is_benchmarking) {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.25f, 0.04f, 0.04f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.00f, 0.20f, 0.20f, 0.80f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.80f, 1.00f, 0.40f));
            }

            ImGuiWindowFlags hud_flags = ImGuiWindowFlags_NoTitleBar;

            ImGui::Begin("HUD_CompactPill", nullptr, hud_flags);
            
            // Fila 0: Barra de Arrastre
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.5f, 1.0f));
            ImGui::TextUnformatted(":: GAMEPERFTRACE");
            ImGui::PopStyleColor();

            // Lógica de movimiento con el ratón
            double cx, cy;
            glfwGetCursorPos(window, &cx, &cy);

            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                is_dragging_hud = true;
                drag_start_offset_x = cx;
                drag_start_offset_y = cy;
            }

            if (is_dragging_hud) {
                if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                    int wx, wy;
                    glfwGetWindowPos(window, &wx, &wy);
                    int new_wx = wx + static_cast<int>(cx - drag_start_offset_x);
                    int new_wy = wy + static_cast<int>(cy - drag_start_offset_y);
                    glfwSetWindowPos(window, new_wx, new_wy);
                    hud_win_x = new_wx;
                    hud_win_y = new_wy;
                } else {
                    is_dragging_hud = false;
                }
            }

            // Sincronización de redimensionado GLFW / ImGui
            ImVec2 current_imgui_size = ImGui::GetWindowSize();
            int cur_glfw_w, cur_glfw_h;
            glfwGetWindowSize(window, &cur_glfw_w, &cur_glfw_h);

            if (std::abs(static_cast<int>(current_imgui_size.x) - cur_glfw_w) > 2 ||
                std::abs(static_cast<int>(current_imgui_size.y) - cur_glfw_h) > 2) {
                hud_win_w = static_cast<int>(current_imgui_size.x);
                hud_win_h = static_cast<int>(current_imgui_size.y);
                glfwSetWindowSize(window, hud_win_w, hud_win_h);
            }

            // Fila 1: FPS, 1% Low e Indicador REC
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "%.0f FPS", current_fps);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(1%%: %.0f)", fps_1percent_low);

            if (is_benchmarking) {
                int elapsed_s = static_cast<int>(now - benchmark_start_time);
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "REC %ds", elapsed_s);
            }

            // Fila 2: CPU % y Temperatura
            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "CPU: %.0f%%", cpu_history.back());
            if (current_temp > 0.0f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "| %.0f°C", current_temp);
            }

            // Fila 3: GPU % y VRAM (Si está disponible)
            if (current_gpu.available) {
                ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "GPU: %.0f%%", current_gpu.usage_pct);
                if (current_gpu.temp_c > 0.0f) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "| %.0f°C", current_gpu.temp_c);
                }
                if (current_gpu.vram_used_mb > 0.0f) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "| VRAM: %.0fMB", current_gpu.vram_used_mb);
                }
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "GPU: N/A");
            }

            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

        } else {
            // --- MODO 2: DASHBOARD PRO COMPLETO ---
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);

            ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

            ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "GAMEPERFTRACE HARDWARE MONITOR");
            ImGui::SameLine(ImGui::GetWindowWidth() - 580);

            if (is_benchmarking) {
                int elapsed_s = static_cast<int>(now - benchmark_start_time);
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[F11: PARAR REC %ds]", elapsed_s);
            } else {
                ImGui::Text("F11: Iniciar REC | F12: HUD");
            }

            ImGui::SameLine();
            ImGui::Text(" | Uptime: %s", formatUptime(monitor.getUptimeSeconds()).c_str());

            ImGui::Separator();
            ImGui::Spacing();

            if (show_benchmark_result && !is_benchmarking) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.35f, 0.15f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.20f, 0.90f, 0.40f, 0.60f));
                ImGui::BeginChild("BenchNotice", ImVec2(0, 50), true);
                
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[OK] BENCHMARK GUARDADO:");
                ImGui::SameLine();
                ImGui::Text("Duracion: %.1fs | Avg FPS: %.0f | 1%% Low: %.0f | Archivo: %s", 
                            last_bench_duration, last_bench_avg_fps, last_bench_1low_fps, last_csv_filename.c_str());

                ImGui::EndChild();
                ImGui::PopStyleColor(2);
                ImGui::Spacing();
            }

            // Tarjetas KPI de Hardware (5 Columnas)
            ImGui::Columns(5, "kpis", false);

            char cpu_val[32];
            if (current_temp > 0.0f) snprintf(cpu_val, sizeof(cpu_val), "%.1f%% (%.0f°C)", cpu_history.back(), current_temp);
            else snprintf(cpu_val, sizeof(cpu_val), "%.1f%%", cpu_history.back());
            RenderKpiCard("PROCESADOR (CPU)", cpu_val, cpu_history.back() > 85.0f ? "Pico de Carga" : "Carga Normal", ImVec4(0.2f, 0.7f, 1.0f, 1.0f));

            ImGui::NextColumn();

            char gpu_val[64];
            if (current_gpu.available) {
                if (current_gpu.temp_c > 0.0f) snprintf(gpu_val, sizeof(gpu_val), "%.1f%% (%.0f°C)", current_gpu.usage_pct, current_gpu.temp_c);
                else snprintf(gpu_val, sizeof(gpu_val), "%.1f%%", current_gpu.usage_pct);
            } else {
                snprintf(gpu_val, sizeof(gpu_val), "No detectada");
            }
            char gpu_st[64];
            if (current_gpu.vram_total_mb > 0.0f) snprintf(gpu_st, sizeof(gpu_st), "VRAM: %.0f/%.0f MB", current_gpu.vram_used_mb, current_gpu.vram_total_mb);
            else snprintf(gpu_st, sizeof(gpu_st), "Driver Activo");
            RenderKpiCard("GRAFICA (GPU)", gpu_val, gpu_st, ImVec4(0.8f, 0.4f, 1.0f, 1.0f));

            ImGui::NextColumn();

            char ram_val[32];
            snprintf(ram_val, sizeof(ram_val), "%.0f MB", ram_history.back());
            float ram_pct = specs.total_ram_mb > 0 ? (ram_history.back() / specs.total_ram_mb) * 100.0f : 0.0f;
            char ram_st[32];
            snprintf(ram_st, sizeof(ram_st), "%.1f%% en uso", ram_pct);
            RenderKpiCard("MEMORIA RAM", ram_val, ram_st, ImVec4(0.7f, 0.3f, 1.0f, 1.0f));

            ImGui::NextColumn();

            char fps_val[32];
            snprintf(fps_val, sizeof(fps_val), "%.0f FPS", current_fps);
            char fps_st[64];
            snprintf(fps_st, sizeof(fps_st), "1%% Low: %.0f FPS", fps_1percent_low);
            ImVec4 fps_color = (fps_1percent_low < current_fps * 0.6f) ? ImVec4(1.0f, 0.4f, 0.2f, 1.0f) : ImVec4(0.2f, 0.9f, 0.4f, 1.0f);
            RenderKpiCard("RENDIMIENTO JUEGO", fps_val, fps_st, fps_color);

            ImGui::NextColumn();

            std::string disk_val = formatRate(disk_read_history.back());
            RenderKpiCard("DISCO (LECTURA)", disk_val.c_str(), disk_read_history.back() > 10000.0f ? "Cargando Archivos" : "Disco Reposado", ImVec4(1.0f, 0.6f, 0.2f, 1.0f));

            ImGui::Columns(1);
            ImGui::Spacing();
            ImGui::Separator();

            if (ImGui::BeginTabBar("Tabs")) {
                if (ImGui::BeginTabItem(" Telemetria en Vivo ")) {
                    ImGui::Spacing();

                    float available_height = ImGui::GetContentRegionAvail().y;
                    float dynamic_plot_height = (available_height - 30.0f) / 2.0f; 
                    dynamic_plot_height = std::max(150.0f, dynamic_plot_height);

                    ImGui::Columns(2, "row1", false);
                    
                    char ft_lbl[64];
                    snprintf(ft_lbl, sizeof(ft_lbl), "Frametime: %.1f ms | 1%% Low: %.0f FPS", current_frametime_ms, fps_1percent_low);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.9f, 0.4f, 1.0f));
                    ImGui::PlotLines("##Frametime", frametime_history.data(), static_cast<int>(frametime_history.size()), 0, ft_lbl, 0.0f, 50.0f, ImVec2(-1, dynamic_plot_height));
                    ImGui::PopStyleColor();

                    ImGui::NextColumn();

                    char gpu_lbl[64];
                    snprintf(gpu_lbl, sizeof(gpu_lbl), "GPU Load: %.1f%%", gpu_history.back());
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.8f, 0.4f, 1.0f, 1.0f));
                    ImGui::PlotLines("##GPU", gpu_history.data(), MAX_HARDWARE_SAMPLES, 0, gpu_lbl, 0.0f, 100.0f, ImVec2(-1, dynamic_plot_height));
                    ImGui::PopStyleColor();

                    ImGui::Columns(1);
                    ImGui::Spacing();

                    ImGui::Columns(2, "row2", false);

                    char cpu_lbl[64];
                    snprintf(cpu_lbl, sizeof(cpu_lbl), "CPU Load: %.1f%%", cpu_history.back());
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
                    ImGui::PlotLines("##CPU", cpu_history.data(), MAX_HARDWARE_SAMPLES, 0, cpu_lbl, 0.0f, 100.0f, ImVec2(-1, dynamic_plot_height));
                    ImGui::PopStyleColor();

                    ImGui::NextColumn();

                    char ram_lbl[64];
                    snprintf(ram_lbl, sizeof(ram_lbl), "RAM Used: %.0f / %lu MB", ram_history.back(), specs.total_ram_mb);
                    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.7f, 0.3f, 1.0f, 1.0f));
                    ImGui::PlotLines("##RAM", ram_history.data(), MAX_HARDWARE_SAMPLES, 0, ram_lbl, 0.0f, static_cast<float>(specs.total_ram_mb), ImVec2(-1, dynamic_plot_height));
                    ImGui::PopStyleColor();

                    ImGui::Columns(1);
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem(" Especificaciones del Sistema ")) {
                    ImGui::Spacing();

                    if (ImGui::CollapsingHeader(" Procesador (CPU)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Modelo CPU: "); ImGui::SameLine(); ImGui::Text("%s", specs.cpu_model.c_str());
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Fabricante: "); ImGui::SameLine(); ImGui::Text("%s", specs.cpu_vendor.c_str());
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Nucleos Logicos: "); ImGui::SameLine(); ImGui::Text("%u Hilos", specs.cpu_cores);
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Frecuencia Base: "); ImGui::SameLine(); ImGui::Text("%s", specs.cpu_freq_mhz.c_str());
                        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Cache L3: "); ImGui::SameLine(); ImGui::Text("%s", specs.cpu_cache_l3.c_str());
                    }

                    if (ImGui::CollapsingHeader(" Tarjeta Grafica (GPU)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Nombre GPU Detectada: "); ImGui::SameLine(); ImGui::Text("%s", current_gpu.name.c_str());
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Ensamblador GPU: "); ImGui::SameLine(); ImGui::Text("%s", specs.gpu_vendor.c_str());
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Modelo Renderer: "); ImGui::SameLine(); ImGui::Text("%s", specs.gpu_renderer.c_str());
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Driver OpenGL: "); ImGui::SameLine(); ImGui::Text("%s", specs.gpu_version.c_str());
                        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Version GLSL: "); ImGui::SameLine(); ImGui::Text("%s", specs.gpu_glsl_version.c_str());
                    }

                    if (ImGui::CollapsingHeader(" Memoria RAM & Paginacion", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "RAM Fisica Total: "); ImGui::SameLine(); 
                        ImGui::Text("%lu MB (%.2f GB)", specs.total_ram_mb, static_cast<double>(specs.total_ram_mb) / 1024.0);
                        
                        ImGui::TextColored(ImVec4(0.8f, 0.4f, 1.0f, 1.0f), "Memoria Swap: "); ImGui::SameLine(); 
                        ImGui::Text("%lu MB (%.2f GB)", specs.total_swap_mb, static_cast<double>(specs.total_swap_mb) / 1024.0);
                    }

                    if (ImGui::CollapsingHeader(" Unidades de Almacenamiento", ImGuiTreeNodeFlags_DefaultOpen)) {
                        for (const auto& drive : specs.drives) {
                            uint64_t used_gb = drive.total_gb - drive.free_gb;
                            float used_percent = drive.total_gb > 0 ? (static_cast<float>(used_gb) / drive.total_gb) * 100.0f : 0.0f;
                            
                            ImGui::Text("Montaje: %s", drive.mount_point.c_str());
                            ImGui::Text("Capacidad: %lu GB Total | %lu GB Libre (%lu GB Usado)", drive.total_gb, drive.free_gb, used_gb);
                            
                            char progress_lbl[32];
                            snprintf(progress_lbl, sizeof(progress_lbl), "%.1f%% ocupado", used_percent);
                            ImGui::ProgressBar(used_percent / 100.0f, ImVec2(-1, 28), progress_lbl);
                        }
                    }

                    if (ImGui::CollapsingHeader(" Sistema Operativo & Kernel", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Sistema: "); ImGui::SameLine(); ImGui::Text("%s", specs.os_name.c_str());
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Version Kernel: "); ImGui::SameLine(); ImGui::Text("%s", specs.kernel_version.c_str());
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Arquitectura: "); ImGui::SameLine(); ImGui::Text("%s", specs.architecture.c_str());
                        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Nombre Host: "); ImGui::SameLine(); ImGui::Text("%s", specs.hostname.c_str());
                    }

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            ImGui::End();
        }

        ImGui::Render();

        if (is_hud_mode) {
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        } else {
            glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        }

        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}