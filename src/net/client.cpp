#include "net/client.h"

#include <ws2tcpip.h>

#include <algorithm>

#include "net/protocol.h"

namespace net {

namespace {

constexpr uint64_t kHelloIntervalMs = 1000;    // while connecting
constexpr uint64_t kRehelloIntervalMs = 3000;  // keep-alive while connected

}  // namespace

Client::~Client() { Stop(); }

bool Client::Start(const std::string& host, uint16_t port, const std::string& name) {
    Stop();

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) {
        SetError("cannot resolve server address '" + host + "'");
        return false;
    }
    server_ = *reinterpret_cast<sockaddr_in*>(result->ai_addr);
    server_.sin_port = htons(port);
    freeaddrinfo(result);

    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nonBlocking = 1;
    ioctlsocket(socket_, FIONBIO, &nonBlocking);

    name_ = name;
    seq_ = 0;
    lastHelloMs_ = 0;

    std::lock_guard lock(mutex_);
    status_ = Status::Connecting;
    error_.clear();
    playerId_ = 0;
    players_.clear();
    return true;
}

void Client::Stop() {
    if (socket_ == INVALID_SOCKET) return;
    if (GetStatus() == Status::Connected) Send(ByeMsg{});
    closesocket(socket_);
    socket_ = INVALID_SOCKET;

    std::lock_guard lock(mutex_);
    status_ = Status::Stopped;
    players_.clear();
}

template <typename T>
void Client::Send(const T& msg) {
    sendto(socket_, reinterpret_cast<const char*>(&msg), sizeof(msg), 0, reinterpret_cast<const sockaddr*>(&server_),
           sizeof(server_));
}

void Client::Update(const LocalState* local) {
    if (socket_ == INVALID_SOCKET) return;
    uint64_t now = GetTickCount64();

    Receive();

    // Hello is also repeated while connected: if the server restarted and forgot us,
    // it registers us again and answers with a fresh Welcome.
    Status status = GetStatus();
    uint64_t helloInterval = status == Status::Connected ? kRehelloIntervalMs : kHelloIntervalMs;
    if (now - lastHelloMs_ >= helloInterval) {
        HelloMsg hello;
        CopyName(hello.name, name_.c_str());
        Send(hello);
        lastHelloMs_ = now;
    }
    if (status == Status::Connected && local) {
        StateMsg state;
        state.seq = ++seq_;
        std::copy(local->pos, local->pos + 3, state.pos);
        state.yaw = local->yaw;
        Send(state);
    }

    std::lock_guard lock(mutex_);
    std::erase_if(players_, [now](const RemotePlayer& p) { return now - p.lastUpdateMs > kTimeoutMs; });
}

void Client::Receive() {
    char buf[512];
    for (;;) {
        sockaddr_in from = {};
        int fromLen = sizeof(from);
        int n = recvfrom(socket_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) {
            // WSAECONNRESET: Windows reports "port unreachable" for an earlier send,
            // i.e. nothing is listening at the server address. Go back to connecting.
            if (n < 0 && WSAGetLastError() == WSAECONNRESET) {
                std::lock_guard lock(mutex_);
                status_ = Status::Connecting;
                error_ = "no server at this address (is mecmm-server running?)";
                players_.clear();
                continue;
            }
            return;
        }
        if (from.sin_addr.s_addr != server_.sin_addr.s_addr || from.sin_port != server_.sin_port) continue;

        std::lock_guard lock(mutex_);
        switch (Parse(buf, n)) {
            case MsgType::Welcome: {
                WelcomeMsg msg;
                memcpy(&msg, buf, sizeof(msg));
                playerId_ = msg.playerId;
                status_ = Status::Connected;
                error_.clear();
                break;
            }
            case MsgType::PlayerState: {
                PlayerStateMsg msg;
                memcpy(&msg, buf, sizeof(msg));
                msg.name[kNameLength - 1] = 0;
                auto it = std::find_if(players_.begin(), players_.end(),
                                       [&](const RemotePlayer& p) { return p.id == msg.playerId; });
                if (it == players_.end()) {
                    it = players_.insert(players_.end(), RemotePlayer{});
                    it->id = msg.playerId;
                } else if (msg.seq <= it->seq) {
                    break;  // older than what we already have (UDP may reorder)
                }
                it->name = msg.name;
                it->seq = msg.seq;
                std::copy(msg.pos, msg.pos + 3, it->pos);
                it->yaw = msg.yaw;
                it->lastUpdateMs = GetTickCount64();
                break;
            }
            case MsgType::PlayerLeft: {
                PlayerLeftMsg msg;
                memcpy(&msg, buf, sizeof(msg));
                std::erase_if(players_, [&](const RemotePlayer& p) { return p.id == msg.playerId; });
                break;
            }
            default:
                break;
        }
    }
}

Status Client::GetStatus() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string Client::GetError() const {
    std::lock_guard lock(mutex_);
    return error_;
}

uint32_t Client::GetPlayerId() const {
    std::lock_guard lock(mutex_);
    return playerId_;
}

std::vector<RemotePlayer> Client::Players() const {
    std::lock_guard lock(mutex_);
    return players_;
}

void Client::SetError(const std::string& error) {
    std::lock_guard lock(mutex_);
    status_ = Status::Error;
    error_ = error;
}

}  // namespace net
