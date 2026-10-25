/**
 * VitaABS - vita-shell-companion bridge
 *
 * The app's side of the SceShell plugin (lib/vita-shell-companion,
 * docs/PROTOCOL.md there). Vita only: everywhere else every call is an inline
 * no-op, so callers need no #ifdefs.
 *
 * Features are switched on one test build at a time. This build registers
 * SC_FEAT_ROW only — the plugin ignores (and logs) commands for features an
 * app did not ask for in its HELLO, so nothing is advertised before the app
 * actually handles it.
 */

#pragma once

#include <string>

namespace vitaabs {
namespace shell {

// A press on the quick menu's Music row, for this app.
enum class Press { None, PlayPause, Next, Previous };

#ifdef __vita__

// Once at start-up: scInit, then scOpen on a worker thread (opening may load
// the plugin into SceShell). The outcome is logged either way.
void init();
bool isOpen();

// ── Music row ──────────────────────────────────────────────────────────
// Heartbeat: call about once a second while the player is up. The plugin
// hands the row back to the system by itself 5 s after the last one.
void rowHeartbeat();
// Give the row back now (the player is going away).
void rowRelease();
// What the row shows: title above, "artist / album" below. Sent only when it
// changed, so it is fine to call every tick.
void rowSong(const std::string& title, const std::string& artist, const std::string& album);
void rowPlaying(bool playing);
// The cover, from the server or the card. Fetched, squared to 64x64 and
// written on a worker thread; a later call supersedes one still in flight.
void rowCoverFromUrl(const std::string& url);
void rowCoverFromFile(const std::string& path);
// A press waiting for this app, or Press::None. Never blocks.
Press poll();

#else

inline void init() {}
inline bool isOpen() { return false; }
inline void rowHeartbeat() {}
inline void rowRelease() {}
inline void rowSong(const std::string&, const std::string&, const std::string&) {}
inline void rowPlaying(bool) {}
inline void rowCoverFromUrl(const std::string&) {}
inline void rowCoverFromFile(const std::string&) {}
inline Press poll() { return Press::None; }

#endif

}  // namespace shell
}  // namespace vitaabs
