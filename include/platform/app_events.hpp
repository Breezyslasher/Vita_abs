/**
 * VitaABS - Vita application events (non-game packaging)
 *
 * The VPK is a non-game application (CATEGORY gdc), so pressing PS leaves it
 * running — audio, network and the UI loop included — where a game is
 * frozen. That brings one obligation a game never has: the system asks a
 * non-game app to QUIT when the user opens something it cannot run beside,
 * and an app that does not answer hangs the console (VitaPlex, log
 * 3ea218f5: the other app sat on its loading screen and the Vita had to be
 * held off with the power button).
 *
 * A watcher thread reads the system's application events: it answers the
 * quit request (the normal shutdown first, then ending the process if that
 * stalls) and logs leaving and returning, with a heartbeat while away so a
 * test log shows whether the app kept running. Vita only; a no-op elsewhere.
 */

#pragma once

namespace vitaabs {
namespace appevents {

#ifdef __vita__
// Once at start-up, on the UI thread (it also starts a UI-loop timer).
void init();
#else
inline void init() {}
#endif

}  // namespace appevents
}  // namespace vitaabs
