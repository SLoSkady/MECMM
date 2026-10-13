#pragma once
// Wire format shared by the mod, the relay server and the test bot.
// All messages are single UDP datagrams; fields are little-endian (x86/x64 only).

#include <cstdint>
#include <cstring>

namespace net {

constexpr uint16_t kDefaultPort = 47777;
constexpr uint32_t kMagic = 0x4D43454D;  // "MECM"
constexpr uint8_t kVersion = 1;
constexpr int kNameLength = 24;            // including the terminating zero
constexpr int kSendRateHz = 30;
constexpr int kTimeoutMs = 5000;           // a player is dropped after this long without packets

enum class MsgType : uint8_t {
    // client -> server
    Hello = 1,        // join with a name
    State = 2,        // my current state
    Bye = 3,          // leaving
    // server -> client
    Welcome = 10,     // your player id
    PlayerState = 11, // state of another player
    PlayerLeft = 12,  // another player left or timed out
};

#pragma pack(push, 1)

struct Header {
    uint32_t magic = kMagic;
    uint8_t version = kVersion;
    MsgType type;
};

struct HelloMsg {
    Header header{.type = MsgType::Hello};
    char name[kNameLength] = {};
};

struct StateMsg {
    Header header{.type = MsgType::State};
    uint32_t seq = 0;   // increases with every packet; lets receivers drop reordered ones
    float pos[3] = {};  // world position, Y up
    float yaw = 0;      // radians, rotation around Y
};

struct ByeMsg {
    Header header{.type = MsgType::Bye};
};

struct WelcomeMsg {
    Header header{.type = MsgType::Welcome};
    uint32_t playerId = 0;
};

struct PlayerStateMsg {
    Header header{.type = MsgType::PlayerState};
    uint32_t playerId = 0;
    char name[kNameLength] = {};
    uint32_t seq = 0;
    float pos[3] = {};
    float yaw = 0;
};

struct PlayerLeftMsg {
    Header header{.type = MsgType::PlayerLeft};
    uint32_t playerId = 0;
};

#pragma pack(pop)

// Returns the message type if `data` holds a valid message of the expected size, else 0.
inline MsgType Parse(const void* data, int size) {
    if (size < static_cast<int>(sizeof(Header))) return MsgType{};
    Header h;
    memcpy(&h, data, sizeof(h));
    if (h.magic != kMagic || h.version != kVersion) return MsgType{};

    int expected = 0;
    switch (h.type) {
        case MsgType::Hello: expected = sizeof(HelloMsg); break;
        case MsgType::State: expected = sizeof(StateMsg); break;
        case MsgType::Bye: expected = sizeof(ByeMsg); break;
        case MsgType::Welcome: expected = sizeof(WelcomeMsg); break;
        case MsgType::PlayerState: expected = sizeof(PlayerStateMsg); break;
        case MsgType::PlayerLeft: expected = sizeof(PlayerLeftMsg); break;
        default: return MsgType{};
    }
    return size == expected ? h.type : MsgType{};
}

inline void CopyName(char (&dst)[kNameLength], const char* src) {
    strncpy_s(dst, src, _TRUNCATE);
}

}  // namespace net
