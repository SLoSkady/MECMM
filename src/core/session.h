#pragma once
// The mod's multiplayer session: a background thread that sends Faith's state to the
// relay server and keeps the list of other players up to date.

#include "net/client.h"

// Starts the network thread (reads mecmm.ini).
void StartSession();

// The session's client, for the UI to read status and players.
const net::Client& Session();
