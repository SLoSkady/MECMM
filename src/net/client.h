#pragma once
// UDP client for the MECMM relay server. Used by the mod and by the test bot.
// Update() is called from one thread; Status()/Players() may be called from any thread.

#include <winsock2.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace net {

struct LocalState {
    float pos[3];
    float yaw;
};

struct RemotePlayer {
    uint32_t id = 0;
    std::string name;
    float pos[3] = {};
    float yaw = 0;
    uint32_t seq = 0;
    uint64_t lastUpdateMs = 0;  // GetTickCount64() when the last state arrived
};

enum class Status { Stopped, Connecting, Connected, Error };

class Client {
public:
    ~Client();

    // Resolves the server address and opens the socket. Does not block on the network.
    bool Start(const std::string& host, uint16_t port, const std::string& name);
    // Tells the server we leave and closes the socket.
    void Stop();

    // Sends our state (or Hello while not yet connected) and processes incoming packets.
    // `local` may be null when there is nothing to send (e.g. loading screen).
    void Update(const LocalState* local);

    Status GetStatus() const;
    std::string GetError() const;
    uint32_t GetPlayerId() const;
    std::vector<RemotePlayer> Players() const;

private:
    void Receive();
    template <typename T>
    void Send(const T& msg);
    void SetError(const std::string& error);

    SOCKET socket_ = INVALID_SOCKET;
    sockaddr_in server_ = {};
    std::string name_;
    uint32_t seq_ = 0;
    uint64_t lastHelloMs_ = 0;

    mutable std::mutex mutex_;  // guards everything below
    Status status_ = Status::Stopped;
    std::string error_;
    uint32_t playerId_ = 0;
    std::vector<RemotePlayer> players_;
};

}  // namespace net
