// MECMM test bot: joins the relay server and runs in circles around the first real player,
// so multiplayer can be tested alone.
// Usage: mecmm-bot.exe [host] [port] [name] [radius_m]
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "net/client.h"
#include "net/protocol.h"

int main(int argc, char** argv) {
    std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(atoi(argv[2])) : net::kDefaultPort;
    std::string name = argc > 3 ? argv[3] : "Bot";
    float radius = argc > 4 ? static_cast<float>(atof(argv[4])) : 5.0f;
    setvbuf(stdout, nullptr, _IONBF, 0);

    net::Client client;
    if (!client.Start(host, port, name)) {
        printf("Error: %s\n", client.GetError().c_str());
        return 1;
    }
    printf("Bot '%s' connecting to %s:%u, circle radius %.1f m. Press Ctrl+C to stop.\n", name.c_str(), host.c_str(),
           port, radius);

    constexpr float kSpeed = 4.0f;  // m/s, a light jog
    const float angularSpeed = kSpeed / radius;
    float angle = 0;
    uint64_t lastPrint = 0;
    net::Status lastStatus = net::Status::Stopped;

    for (;;) {
        // Circle around the first other player; stay silent until there is one.
        auto players = client.Players();
        const net::RemotePlayer* target = players.empty() ? nullptr : &players.front();

        net::LocalState state;
        if (target) {
            angle += angularSpeed / net::kSendRateHz;
            state.pos[0] = target->pos[0] + radius * std::cos(angle);
            state.pos[1] = target->pos[1];
            state.pos[2] = target->pos[2] + radius * std::sin(angle);
            state.yaw = -angle;  // facing along the circle
        }
        client.Update(target ? &state : nullptr);

        net::Status status = client.GetStatus();
        uint64_t now = GetTickCount64();
        if (status != lastStatus || now - lastPrint >= 2000) {
            if (status == net::Status::Connected) {
                if (target)
                    printf("connected as #%u, circling '%s' at (%.1f, %.1f, %.1f)\n", client.GetPlayerId(),
                           target->name.c_str(), target->pos[0], target->pos[1], target->pos[2]);
                else
                    printf("connected as #%u, waiting for a player to join...\n", client.GetPlayerId());
            } else {
                std::string error = client.GetError();
                printf("connecting... %s\n", error.c_str());
            }
            lastStatus = status;
            lastPrint = now;
        }
        Sleep(1000 / net::kSendRateHz);
    }
}
