#pragma once

#include <cstdint>
#include <string>

struct Config {
    std::string server = "127.0.0.1";
    uint16_t port = 0;  // 0 = net::kDefaultPort
    std::string name = "Runner";
};

// Reads mecmm.ini next to the game's .exe; creates it with defaults if missing.
Config LoadConfig();
