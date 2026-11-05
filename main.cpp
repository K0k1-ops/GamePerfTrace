#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "SystemMonitor.hpp"

#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Convierte KB/s a MB/s si supera los 1024 KB/s para mejor lectura
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
    snprintf(buffer, sizeof(buffer), "%lu h %lu m %lu s", hrs, mins, secs);
    return std::string(buffer);
}

// Función auxiliar para renderizar Tarjetas KPI en la cabecera
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

int main() {
    if (!glfwInit()) return -1;

    GLFWwindow* window = glfwCreateWindow(1440, 900, "GamePerfTrace - Dashboard Pro", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    io.FontGlobalScale = 2.0f;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 8.0f;
    style.ItemSpacing = ImVec2(14, 14);
    style.FramePadding = ImVec2(10, 8);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    SystemMonitor monitor;
    SystemInfo specs = monitor.getStaticSystemInfo();

    specs.gpu_vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    specs.gpu_renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    specs.gpu_version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    specs.gpu_glsl_version = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));

    const int MAX_SAMPLES = 100;
    std::vector<float> cpu_history(MAX_SAMPLES, 0.0f);
    std::vector<float> ram_history(MAX_SAMPLES, 0.0f);
    std::vector<float> net_dl_history(MAX_SAMPLES, 0.0f);
    std::vector<float> disk_read_history(MAX_SAMPLES, 0.0f);

    double last_time = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        double current_time = glfwGetTime();
        if (current_time - last_time >= 0.5) {
            float cpu = monitor.getCpuUsage();
            float ram = static_cast<float>(monitor.getRamUsageMB());
            NetworkMetrics net = monitor.getNetworkIO();
            DiskMetrics disk = monitor.getDiskIO();

            std::rotate(cpu_history.begin(), cpu_history.begin() + 1, cpu_history.end());
            cpu_history.back() = cpu;

            std::rotate(ram_history.begin(), ram_history.begin() + 1, ram_history.end());
            ram_history.back() = ram;

            std::rotate(net_dl_history.begin(), net_dl_history.begin() + 1, net_dl_history.end());
            net_dl_history.back() = net.download_kbps;

            std::rotate(disk_read_history.begin(), disk_read_history.begin() + 1, disk_read_history.end());
            disk_read_history.back() = disk.read_kbps;

            last_time = current_time;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);

        ImGui::Begin("MainPanel", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // Cabecera Principal
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "GAMEPERFTRACE HARDWARE MONITOR");
        ImGui::SameLine(ImGui::GetWindowWidth() - 380);
        ImGui::Text("Encendido: %s", formatUptime(monitor.getUptimeSeconds()).c_str());

        ImGui::Separator();
        ImGui::Spacing();

        // 1. TARJETAS DE RESUMEN RÁPIDO (KPI CARDS)
        ImGui::Columns(4, "kpis", false);

        // Tarjeta CPU
        char cpu_val[32];
        float temp = monitor.getCpuTemperature();
        if (temp > 0.0f) snprintf(cpu_val, sizeof(cpu_val), "%.1f%% (%.0f°C)", cpu_history.back(), temp);
        else snprintf(cpu_val, sizeof(cpu_val), "%.1f%%", cpu_history.back());
        const char* cpu_st = cpu_history.back() > 85.0f ? "Pico de Carga" : "Carga Normal";
        RenderKpiCard("PROCESADOR (CPU)", cpu_val, cpu_st, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));

        ImGui::NextColumn();

        // Tarjeta RAM
        char ram_val[32];
        snprintf(ram_val, sizeof(ram_val), "%.0f MB", ram_history.back());
        float ram_pct = specs.total_ram_mb > 0 ? (ram_history.back() / specs.total_ram_mb) * 100.0f : 0.0f;
        char ram_st[32];
        snprintf(ram_st, sizeof(ram_st), "%.1f%% en uso", ram_pct);
        RenderKpiCard("MEMORIA RAM", ram_val, ram_st, ImVec4(0.7f, 0.3f, 1.0f, 1.0f));

        ImGui::NextColumn();

        // Tarjeta RED
        std::string net_val = formatRate(net_dl_history.back());
        const char* net_st = net_dl_history.back() > 2000.0f ? "Descarga Intensa" : "Red Estable";
        RenderKpiCard("RED (DESCARGA)", net_val.c_str(), net_st, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));

        ImGui::NextColumn();

        // Tarjeta DISCO
        std::string disk_val = formatRate(disk_read_history.back());
        const char* disk_st = disk_read_history.back() > 10000.0f ? "Cargando Archivos" : "Disco Reposado";
        RenderKpiCard("DISCO (LECTURA)", disk_val.c_str(), disk_st, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));

        ImGui::Columns(1);
        ImGui::Spacing();
        ImGui::Separator();

        // 2. PESTAÑAS DETALLADAS
        if (ImGui::BeginTabBar("Tabs")) {
            
            // PESTAÑA 1: TELEMETRÍA CON GRÁFICAS EN COLOR Y AUTO-ESCALA
            if (ImGui::BeginTabItem(" Telemetria en Vivo ")) {
                ImGui::Spacing();

                float available_height = ImGui::GetContentRegionAvail().y;
                float dynamic_plot_height = (available_height - 30.0f) / 2.0f; 
                dynamic_plot_height = std::max(150.0f, dynamic_plot_height);

                // Fila 1: CPU (Azul) y RAM (Púrpura)
                ImGui::Columns(2, "row1", false);
                
                char cpu_lbl[64];
                snprintf(cpu_lbl, sizeof(cpu_lbl), "CPU Load: %.1f%%", cpu_history.back());
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.7f, 1.0f, 1.0f));
                ImGui::PlotLines("##CPU", cpu_history.data(), MAX_SAMPLES, 0, cpu_lbl, 0.0f, 100.0f, ImVec2(-1, dynamic_plot_height));
                ImGui::PopStyleColor();

                ImGui::NextColumn();

                char ram_lbl[64];
                snprintf(ram_lbl, sizeof(ram_lbl), "RAM Used: %.0f / %lu MB", ram_history.back(), specs.total_ram_mb);
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.7f, 0.3f, 1.0f, 1.0f));
                ImGui::PlotLines("##RAM", ram_history.data(), MAX_SAMPLES, 0, ram_lbl, 0.0f, static_cast<float>(specs.total_ram_mb), ImVec2(-1, dynamic_plot_height));
                ImGui::PopStyleColor();

                ImGui::Columns(1);
                ImGui::Spacing();

                // Fila 2: RED (Verde) y DISCO (Naranja) con Auto-Escalado
                ImGui::Columns(2, "row2", false);

                // Auto-Escalado de Red
                float max_net = *std::max_element(net_dl_history.begin(), net_dl_history.end());
                float net_scale = std::max(500.0f, max_net * 1.2f);
                std::string net_lbl = "Red Descarga: " + formatRate(net_dl_history.back());
                
                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.2f, 0.9f, 0.5f, 1.0f));
                ImGui::PlotLines("##NET", net_dl_history.data(), MAX_SAMPLES, 0, net_lbl.c_str(), 0.0f, net_scale, ImVec2(-1, dynamic_plot_height));
                ImGui::PopStyleColor();

                ImGui::NextColumn();

                // Auto-Escalado de Disco
                float max_disk = *std::max_element(disk_read_history.begin(), disk_read_history.end());
                float disk_scale = std::max(2000.0f, max_disk * 1.2f);
                std::string disk_lbl = "Disco Lectura: " + formatRate(disk_read_history.back());

                ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
                ImGui::PlotLines("##DISK", disk_read_history.data(), MAX_SAMPLES, 0, disk_lbl.c_str(), 0.0f, disk_scale, ImVec2(-1, dynamic_plot_height));
                ImGui::PopStyleColor();

                ImGui::Columns(1);
                ImGui::EndTabItem();
            }

            // PESTAÑA 2: ESPECIFICACIONES COMPLETAS
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

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
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