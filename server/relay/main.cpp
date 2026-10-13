// MECMM relay server: receives each player's state and forwards it to everyone else.
// Usage: mecmm-server.exe [port]
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "net/protocol.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Player {
    uint32_t id;
    std::string name;
    sockaddr_in addr;
    Clock::time_point lastSeen;
};

// Key: IPv4 address + port of the sender.
uint64_t Key(const sockaddr_in& a) {
    return (static_cast<uint64_t>(a.sin_addr.s_addr) << 16) | a.sin_port;
}

std::string AddrToString(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

template <typename T>
void SendTo(SOCKET s, const sockaddr_in& to, const T& msg) {
    sendto(s, reinterpret_cast<const char*>(&msg), sizeof(msg), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
}

}  // namespace

int main(int argc, char** argv) {
    uint16_t port = argc > 1 ? static_cast<uint16_t>(atoi(argv[1])) : net::kDefaultPort;
    setvbuf(stdout, nullptr, _IONBF, 0);  // show log lines immediately, also when redirected to a file

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(port);
    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        printf("Cannot listen on UDP port %u (error %d). Is another server running?\n", port, WSAGetLastError());
        return 1;
    }
    printf("MECMM relay server listening on UDP port %u. Press Ctrl+C to stop.\n", port);

    std::map<uint64_t, Player> players;
    uint32_t nextId = 1;
    char buf[512];

    for (;;) {
        // Wait up to 500 ms for a packet so timeouts are checked regularly.
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(sock, &readable);
        timeval tv = {0, 500 * 1000};
        if (select(0, &readable, nullptr, nullptr, &tv) > 0) {
            sockaddr_in from = {};
            int fromLen = sizeof(from);
            int n = recvfrom(sock, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            net::MsgType type = n > 0 ? net::Parse(buf, n) : net::MsgType{};
            auto it = players.find(Key(from));

            if (type == net::MsgType::Hello) {
                net::HelloMsg hello;
                memcpy(&hello, buf, sizeof(hello));
                hello.name[net::kNameLength - 1] = 0;
                if (it == players.end()) {
                    Player p{nextId++, hello.name, from, Clock::now()};
                    it = players.emplace(Key(from), p).first;
                    printf("+ %s joined as #%u from %s (%zu online)\n", p.name.c_str(), p.id, AddrToString(from).c_str(),
                           players.size());
                }
                it->second.lastSeen = Clock::now();
                net::WelcomeMsg welcome;
                welcome.playerId = it->second.id;
                SendTo(sock, from, welcome);  // repeated Hello (lost Welcome) gets the same id
            } else if (type == net::MsgType::State && it != players.end()) {
                net::StateMsg state;
                memcpy(&state, buf, sizeof(state));
                it->second.lastSeen = Clock::now();

                net::PlayerStateMsg out;
                out.playerId = it->second.id;
                net::CopyName(out.name, it->second.name.c_str());
                out.seq = state.seq;
                memcpy(out.pos, state.pos, sizeof(out.pos));
                out.yaw = state.yaw;
                for (auto& [key, other] : players)
                    if (key != it->first) SendTo(sock, other.addr, out);
            } else if (type == net::MsgType::Bye && it != players.end()) {
                printf("- %s (#%u) left\n", it->second.name.c_str(), it->second.id);
                net::PlayerLeftMsg left;
                left.playerId = it->second.id;
                players.erase(it);
                for (auto& [key, other] : players) SendTo(sock, other.addr, left);
            }
        }

        // Drop players we have not heard from.
        auto now = Clock::now();
        for (auto it = players.begin(); it != players.end();) {
            if (now - it->second.lastSeen > std::chrono::milliseconds(net::kTimeoutMs)) {
                printf("- %s (#%u) timed out\n", it->second.name.c_str(), it->second.id);
                net::PlayerLeftMsg left;
                left.playerId = it->second.id;
                it = players.erase(it);
                for (auto& [key, other] : players) SendTo(sock, other.addr, left);
            } else {
                ++it;
            }
        }
    }
}
