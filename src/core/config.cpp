#include "core/config.h"

#include <windows.h>

#include "core/log.h"
#include "net/protocol.h"

namespace {

std::wstring IniPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"mecmm.ini";
}

std::string ReadString(const std::wstring& ini, const wchar_t* key, const std::string& fallback) {
    wchar_t buf[256];
    std::wstring def(fallback.begin(), fallback.end());
    GetPrivateProfileStringW(L"net", key, def.c_str(), buf, 256, ini.c_str());
    char utf8[512];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

}  // namespace

Config LoadConfig() {
    Config config;
    config.port = net::kDefaultPort;
    std::wstring ini = IniPath();

    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringW(L"net", L"server", L"127.0.0.1", ini.c_str());
        WritePrivateProfileStringW(L"net", L"port", std::to_wstring(config.port).c_str(), ini.c_str());
        WritePrivateProfileStringW(L"net", L"name", L"Runner", ini.c_str());
        Log("config: created mecmm.ini with defaults");
    }

    config.server = ReadString(ini, L"server", config.server);
    config.port = static_cast<uint16_t>(GetPrivateProfileIntW(L"net", L"port", config.port, ini.c_str()));
    config.name = ReadString(ini, L"name", config.name);
    Log("config: server=%s port=%u name=%s", config.server.c_str(), config.port, config.name.c_str());
    return config;
}
