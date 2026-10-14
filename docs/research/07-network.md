# 7. Network prototype

**Goal:** share every player's position and heading with everyone else, simply and robustly.

## Design

- **UDP**, one datagram per message (`src/net/protocol.h`). Positions are sent 30 times a second;
  a lost update is replaced by the next one 33 ms later, so retransmission (TCP) would only add delay.
- **Relay server** (`server/relay/main.cpp`): clients never talk to each other directly. Only the
  server needs a reachable port; it forwards each player's state to all others.
- Every message starts with a header: magic `"MECM"`, protocol version, message type. Messages of
  the wrong size or version are ignored.

| Message | Direction | Content |
|---|---|---|
| `Hello` | client → server | name (join; repeated every 3 s as keep-alive) |
| `Welcome` | server → client | the player's id |
| `State` | client → server | sequence number, position, yaw |
| `PlayerState` | server → client | id, name, sequence, position, yaw of another player |
| `Bye` / `PlayerLeft` | both | leaving / someone left or timed out (5 s of silence) |

## Robustness

- **Sequence numbers**: UDP may reorder packets; a state older than the last one received is dropped.
- **Keep-alive Hello**: if the server restarts and forgets a client, the next `Hello` registers it
  again and the session continues by itself.
- **"Port unreachable"**: Windows reports `WSAECONNRESET` on a UDP socket when an earlier packet hit a
  closed port; the client shows "no server at this address" and keeps retrying.

## Smoothing

States arrive every ~33 ms, frames every ~16 ms. Each remote player keeps the last ~0.5 s of states
with arrival times (a high-resolution clock: `GetTickCount64` ticks in ~16 ms steps). It is drawn
**100 ms in the past**, interpolated between the two states around that moment (angles along the
shorter way). The delay costs a little latency and removes the stutter.

## Test bot

`server/bot/main.cpp` joins as a player and circles 5 m around the first real player, so everything
can be tested alone. It shares the client code with the mod (`mecmm_net` library).
