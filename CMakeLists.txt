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
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)

# Networking shared by the mod, the relay server and the test bot.
add_library(mecmm_net STATIC src/net/client.cpp)
target_include_directories(mecmm_net PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mecmm_net PUBLIC ws2_32)

add_library(mecmm SHARED
    src/core/dllmain.cpp
    src/core/proxy.cpp
    src/core/log.cpp
    src/core/config.cpp
    src/core/session.cpp
    src/core/dinput8.def
    src/game/game.cpp
    src/overlay/overlay.cpp
)
set_target_properties(mecmm PROPERTIES OUTPUT_NAME dinput8)
target_link_libraries(mecmm PRIVATE mecmm_net imgui minhook d3d11 dxgi)

# Relay server and test bot (console programs).
add_executable(mecmm-server server/relay/main.cpp)
target_link_libraries(mecmm-server PRIVATE mecmm_net)

add_executable(mecmm-bot server/bot/main.cpp)
target_link_libraries(mecmm-bot PRIVATE mecmm_net)
