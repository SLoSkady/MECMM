cmake_minimum_required(VERSION 3.20)
project(MECMM LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the DLL must not depend on a VC++ redistributable being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)
set(MINHOOK_DIR ${CMAKE_SOURCE_DIR}/external/minhook)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)

add_library(minhook STATIC
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)

# The mod itself. Built as dinput8.dll: the game loads it from its own folder,
# and we forward the real DirectInput calls to the system dinput8.dll.
add_library(mecmm SHARED
    src/core/dllmain.cpp
    src/core/proxy.cpp
    src/core/log.cpp
    src/core/dinput8.def
    src/game/game.cpp
    src/overlay/overlay.cpp
)
set_target_properties(mecmm PROPERTIES OUTPUT_NAME dinput8)
target_include_directories(mecmm PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mecmm PRIVATE imgui minhook d3d11 dxgi)
target_compile_definitions(mecmm PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
