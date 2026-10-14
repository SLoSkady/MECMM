#pragma once

#include <cstdint>
#include <string>

struct Config {
    std::string server = "127.0.0.1";
    uint16_t port = 0;  // 0 = net::kDefaultPort
    std::string name = "Runner";

    // Virtual-key codes of the hotkeys ([keys] section, written as names like F7, Home, PageUp, K).
    int keyMenu = 0;   // show/hide the MECMM window
    int keyDebug = 0;  // show/hide the depth debug window
};

// Reads mecmm.ini next to the game's .exe; creates it (or missing entries) with defaults.
// Loaded once; later calls return the same values.
const Config& GetConfig();

// Name <-> virtual-key code for the hotkeys ("F7", "Home", "Insert", "A", "5", ...).
int KeyFromName(const std::string& name);
std::string KeyName(int vk);
