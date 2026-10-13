#include "core/session.h"

#include <windows.h>

#include "core/config.h"
#include "core/log.h"
#include "game/game.h"
#include "net/protocol.h"

namespace {

net::Client g_client;

DWORD WINAPI SessionThread(LPVOID) {
    Config config = LoadConfig();
    if (!g_client.Start(config.server, config.port, config.name)) {
        Log("session: %s", g_client.GetError().c_str());
        return 0;
    }
    Log("session: connecting to %s:%u as '%s'", config.server.c_str(), config.port, config.name.c_str());

    net::Status lastStatus = net::Status::Connecting;
    for (;;) {
        Vec3 pos;
        float yaw = 0;
        bool inGame = GetPlayerPosition(pos) && GetPlayerYaw(yaw);
        net::LocalState state{{pos.x, pos.y, pos.z}, yaw};
        g_client.Update(inGame ? &state : nullptr);

        net::Status status = g_client.GetStatus();
        if (status != lastStatus) {
            if (status == net::Status::Connected)
                Log("session: connected, player id %u", g_client.GetPlayerId());
            else
                Log("session: disconnected (%s)", g_client.GetError().c_str());
            lastStatus = status;
        }
        Sleep(1000 / net::kSendRateHz);
    }
}

}  // namespace

void StartSession() {
    if (HANDLE thread = CreateThread(nullptr, 0, SessionThread, nullptr, 0, nullptr)) CloseHandle(thread);
}

const net::Client& Session() { return g_client; }
