---
name: Cross-Platform Smoke Test
about: Use this checklist to verify a new build works on every supported platform
title: "[Smoke Test] <platform> — <build version>"
labels: testing, multiplatform
assignees: ''
---

# Cross-Platform Smoke Test Checklist

Use this checklist to confirm VitaABS works on the platform under test.
Focus on basic functionality only — not full regression testing.

**Platform tested:** <!-- PS Vita / PS4 / Switch / Android / Android TV / Desktop -->
**Device:** <!-- e.g. PS Vita PCH-2000, Steam Deck, Switch OLED, Pixel 6 -->
**Build commit / version:**
**Audiobookshelf server version:**

---

## App Startup & Connection

* [ ] App launches without crashing
* [ ] Login screen appears
* [ ] Enter server URL and credentials
* [ ] Login succeeds and libraries load
* [ ] App does not freeze during startup
* [ ] Auth token + server URL persist after restart

---

## Home Screen

* [ ] Home tab loads without errors
* [ ] "Continue Listening" row populates with in-progress items
* [ ] "Recently Added" rows populate
* [ ] Cover art / thumbnails load on home rows
* [ ] Horizontal scroll within a row works (DPAD / touch)
* [ ] Selecting a continue-listening item resumes playback at the correct offset

---

## Library & Navigation

* [ ] Audiobooks library loads
* [ ] Podcasts library loads
* [ ] Book covers display correctly
* [ ] Book titles display correctly under covers
* [ ] Filter / sort options work
* [ ] Pagination loads the next page when scrolling to the bottom
* [ ] No blank strips while scrolling
* [ ] Sidebar / tab navigation between sections works
* [ ] No UI layout issues (cut-off text, oversized elements)

---

## Audiobook Detail

* [ ] Open an audiobook's detail page
* [ ] Cover art loads in the detail header
* [ ] Title, author, narrator show correctly
* [ ] Description / summary shows
* [ ] Chapter list populates with titles + durations
* [ ] File list shows audio files
* [ ] Progress bar reflects current listening position
* [ ] "Mark as Finished" / "Mark as Not Started" works and persists

---

## Podcast Detail

* [ ] Open a podcast's detail page
* [ ] Podcast cover art loads
* [ ] Episode list populates with titles + durations
* [ ] Episode descriptions show
* [ ] Selecting an episode opens its detail or starts playback

---

## Player

* [ ] Audiobook plays back smoothly
* [ ] Audio is clear (no crackling / distortion)
* [ ] Pause / play works
* [ ] Seek forward / backward works
* [ ] Seek bar shows correct position
* [ ] Chapter skip forward / backward works
* [ ] Playback speed change works (1x, 1.5x, 2x)
* [ ] Volume control works
* [ ] Sleep timer works (if implemented)
* [ ] Progress is reported to the server (syncs back to ABS)
* [ ] Exit player returns to detail view in the correct state
* [ ] Resume from a partially-listened item starts at the saved offset
* [ ] Podcast episode plays back correctly

---

## Search

* [ ] Search tab / function opens
* [ ] Typing a query returns results
* [ ] Results include audiobooks and podcasts as appropriate
* [ ] Selecting a result opens its detail page

---

## Downloads

* [ ] Downloads tab opens without crashing
* [ ] Download an audiobook from its detail page
* [ ] Download progress updates
* [ ] Downloaded item appears in downloads list
* [ ] Downloaded item plays offline
* [ ] Multi-file audiobook downloads and concatenates correctly
* [ ] Delete a downloaded item works

---

## Settings

* [ ] Settings menu opens
* [ ] Changing audio quality saves and applies on next playback
* [ ] Server URL / connection settings display correctly
* [ ] Changing a setting persists after app restart
* [ ] Logout works and returns to login screen

---

## Platform-Specific Checks

* [ ] Primary input works (controller / touch / keyboard / remote)
* [ ] D-pad navigation moves focus correctly
* [ ] Back / Circle / Esc returns to the previous screen
* [ ] UI scaling looks correct for the platform's resolution
* [ ] No missing icons or fonts
* [ ] No crashes when switching tabs rapidly
* [ ] App works after a clean restart
* [ ] System back button doesn't kill mid-playback unexpectedly

---

## Super Quick Test (minimum)

If short on time, verify:

* [ ] App launches
* [ ] Login to Audiobookshelf server
* [ ] Open audiobooks library
* [ ] Open an audiobook's detail page
* [ ] Start playback
* [ ] Pause / seek / resume
* [ ] Exit player
* [ ] Settings open
* [ ] Restart app successfully

---

## Notes / Issues Found

<!-- Paste logs, screenshots, or describe any problems below. -->
