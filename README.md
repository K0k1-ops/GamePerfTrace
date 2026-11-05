cat << 'EOF' > README.md
# 🚀 GamePerfTrace — Cross-Platform Hardware Profiler

**GamePerfTrace** es una herramienta ligera y de alto rendimiento desarrollada en C++17 para la monitorización en tiempo real de recursos del sistema (CPU, GPU, RAM, Red y Disco) durante sesiones de juego y cargas de trabajo intensas.

![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3-green.svg)
![Dear ImGui](https://img.shields.io/badge/GUI-Dear%20ImGui-orange.svg)
![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)
![License](https://img.shields.io/badge/License-MIT-brightgreen.svg)

---

## 🌟 Características Principales

- ⚡ **Telemetría en Tiempo Real:** Monitorización continua de uso de CPU, temperatura, hilos lógicos y uso de memoria RAM.
- 📊 **Auto-Escalado I/O:** Gráficas adaptativas con tarjetas KPI para tráfico de Red (KB/s a MB/s) y tasa de Lectura/Escritura en Disco.
- 🖥️ **Especificaciones Completas:** Detección detallada del modelo de procesador, GPU (vía OpenGL), Swap y particiones de disco montadas.
- 🎨 **UI Responsiva y Moderna:** Interfaz organizada por pestañas con tema oscuro impulsada por **Dear ImGui** y renderizada nativamente mediante **OpenGL**.
- 🛠️ **Arquitectura Limpia:** Abstracción multiplataforma nativa (Win32 API / `/proc` filesystem) sin dependencias pesadas ni frameworks web.

---

## 🛠️ Requisitos e Instalación

### Linux (Ubuntu / Debian)

1. **Instalar dependencias del sistema:**
   ```bash
   sudo apt update
   sudo apt install git cmake build-essential libgl1-mesa-dev libxinerama-dev libxcursor-dev libxi-dev libxrandr-dev -y