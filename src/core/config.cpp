#include "core/config.h"

#include <windows.h>

#include <cctype>

#include "core/log.h"
#include "net/protocol.h"

namespace {

constexpr char kDefaultKeyMenu[] = "F7";
constexpr char kDefaultKeyDebug[] = "F8";

std::wstring IniPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    return p.substr(0, p.find_last_of(L"\\/") + 1) + L"mecmm.ini";
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Reads [section] key; if it is missing, writes `fallback` there so the user can see and edit it.
std::string ReadOrCreate(const std::wstring& ini, const wchar_t* section, const wchar_t* key, const std::string& fallback) {
    wchar_t buf[256];
    DWORD n = GetPrivateProfileStringW(section, key, L"\x01", buf, 256, ini.c_str());
    if (n == 1 && buf[0] == L'\x01') {
        WritePrivateProfileStringW(section, key, Widen(fallback).c_str(), ini.c_str());
        return fallback;
    }
    char utf8[512];
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, sizeof(utf8), nullptr, nullptr);
    return utf8;
}

struct NamedKey {
    const char* name;
    int vk;
};

constexpr NamedKey kNamedKeys[] = {
    {"Insert", VK_INSERT}, {"Delete", VK_DELETE}, {"Home", VK_HOME},      {"End", VK_END},
    {"PageUp", VK_PRIOR},  {"PageDown", VK_NEXT}, {"Pause", VK_PAUSE},    {"ScrollLock", VK_SCROLL},
    {"Tab", VK_TAB},       {"Backquote", VK_OEM_3}, {"Numpad0", VK_NUMPAD0}, {"NumpadPlus", VK_ADD},
};

Config Load() {
    Config config;
    config.port = net::kDefaultPort;
    std::wstring ini = IniPath();

    config.server = ReadOrCreate(ini, L"net", L"server", config.server);
    config.port = static_cast<uint16_t>(std::stoi(ReadOrCreate(ini, L"net", L"port", std::to_string(config.port))));
    config.name = ReadOrCreate(ini, L"net", L"name", config.name);

    std::string menu = ReadOrCreate(ini, L"keys", L"menu", kDefaultKeyMenu);
    std::string debug = ReadOrCreate(ini, L"keys", L"debug", kDefaultKeyDebug);
    config.keyMenu = KeyFromName(menu);
    config.keyDebug = KeyFromName(debug);
    if (!config.keyMenu) {
        Log("config: unknown key '%s' for menu, using %s", menu.c_str(), kDefaultKeyMenu);
        config.keyMenu = KeyFromName(kDefaultKeyMenu);
    }
    if (!config.keyDebug) {
        Log("config: unknown key '%s' for debug, using %s", debug.c_str(), kDefaultKeyDebug);
        config.keyDebug = KeyFromName(kDefaultKeyDebug);
    }

    Log("config: server=%s port=%u name=%s keys: menu=%s debug=%s", config.server.c_str(), config.port,
        config.name.c_str(), KeyName(config.keyMenu).c_str(), KeyName(config.keyDebug).c_str());
    return config;
}

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i]; ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return i == a.size() && !b[i];
}

}  // namespace

const Config& GetConfig() {
    static const Config config = Load();
    return config;
}

int KeyFromName(const std::string& name) {
    if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
        return std::toupper(static_cast<unsigned char>(name[0]));  // VK codes of A-Z and 0-9 are their ASCII codes
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        int n = atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    for (const auto& k : kNamedKeys)
        if (EqualsIgnoreCase(name, k.name)) return k.vk;
    return 0;
}

std::string KeyName(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return "F" + std::to_string(vk - VK_F1 + 1);
    for (const auto& k : kNamedKeys)
        if (k.vk == vk) return k.name;
    return "?";
}
