cmake_minimum_required(VERSION 3.14)
project(GamePerfTrace CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. Descargar GLFW (Gestión de Ventanas y Contexto OpenGL)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
FetchContent_MakeAvailable(glfw)

# 2. Descargar Dear ImGui (Interfaz Gráfica)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.89.9
)
FetchContent_MakeAvailable(imgui)

# Archivos de ImGui necesarios para OpenGL3 y GLFW
set(IMGUI_SRCS
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# Definición del ejecutable (solo una vez con todos los archivos .cpp)
add_executable(GamePerfTrace 
    main.cpp 
    SystemMonitor.cpp 
    ${IMGUI_SRCS}
)

target_include_directories(GamePerfTrace PRIVATE 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)

find_package(OpenGL REQUIRED)

# Enlace de librerías condicional según el sistema operativo
if(WIN32)
    target_link_libraries(GamePerfTrace PRIVATE glfw OpenGL::GL)
    if(MSVC)
        # Oculta la consola de comandos de Windows al arrancar
        target_link_options(GamePerfTrace PRIVATE "/SUBSYSTEM:WINDOWS" "/ENTRY:mainCRTStartup")
    endif()
else()
    target_link_libraries(GamePerfTrace PRIVATE glfw OpenGL::GL pthread dl)
endif()