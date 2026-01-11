# VitaABS Complete Feature Testing Checklist

## Status Legend

| Symbol | Status |
|--------|--------|
| ✓ | Working |
| X | Broken |
| _(no symbol)_ | Untested |

---

## Authentication & Login

- [X] Login with username/password
- [X] Login credentials persist after app restart
- [X] Logout works correctly
- [X] App handles invalid credentials gracefully
- [X] App starts in offline mode when no network (with downloaded content)

## Library Browsing

- [X] All libraries from server appear as tabs
- [X] Audiobook libraries show book content
- [X] Podcast libraries show podcast content
- [X] Browse all items in a library (scroll through grid)
- [X] View collections within library collection crash
- [X] Recently added items display correctly
- [X] Item covers load properly
- [ ] Progress bars show on partially-played items
- [X] Sidebar collapse option works
- [X] Hidden libraries don't appear in sidebar

## Search

- [X] Search tab opens correctly
- [X] Search returns results from server
- [X] Search results show covers and titles
- [X] Clicking search result opens detail view

## Media Detail View

- [X] Cover image displays
- [X] Title and author display correctly
- [X] Duration shows correctly
- [X] Description/summary displays
- [X] Chapter list shows (for audiobooks with chapters)
- [ ] Chapter UI update
- [ ] Play button starts playback
- [X] Download button queues download
- [X] Delete button removes downloaded content

## Podcast Features

- [X] Podcast detail shows episode list
- [X] Episode list scrolls properly
- [X] Individual episode download works
- [X] "Find New Episodes" checks for new content
- [X] Batch download episodes works
- [X] Episode progress shows correctly

## Playback - Basic Controls

- [X] Play/Pause toggle works
- [X] Progress slider shows current position
- [X] Elapsed time displays correctly
- [X] Remaining time displays correctly
- [X] Cover art shows in player
- [X] Title and author show in player

## Playback - Seeking

- [ ] Skip forward button works
- [ ] Skip backward button works
- [ ] Dragging progress slider seeks correctly
- [ ] Seek interval setting changes skip amount (5s/10s/15s/30s/60s)

## Playback - Speed Control

- [ ] Speed button shows current speed
- [ ] Can change to 0.5x speed
- [ ] Can change to 0.75x speed
- [ ] Can change to 1.0x speed (normal)
- [ ] Can change to 1.25x speed
- [ ] Can change to 1.5x speed
- [ ] Can change to 1.75x speed
- [ ] Can change to 2.0x speed
- [ ] Speed persists when changing tracks

## Playback - Progress & Resume

- [ ] Progress saves when stopping playback
- [ ] Progress saves periodically during playback (every 30s)
- [ ] Progress syncs to server (when online)
- [ ] Resume playback starts at saved position
- [ ] Progress syncs FROM server on app start
- [ ] Finishing content marks it as complete

## Multi-Track Audiobooks

- [ ] Multi-track books detected correctly
- [ ] Correct track downloads based on resume position
- [ ] Playback starts at correct position within track
- [ ] Remaining tracks download in background
- [ ] Background download progress shows in player
- [ ] Combined file plays seamlessly after all tracks download

## Downloads - Basic

- [ ] Download button queues item
- [ ] Download progress shows percentage
- [ ] Download completes successfully
- [ ] Downloaded item appears in Downloads tab
- [ ] Downloaded item plays offline
- [ ] Delete downloaded item works

## Downloads - Management

- [ ] Downloads tab shows all downloaded items
- [ ] Download queue processes items
- [ ] Pause downloads works
- [ ] Cancel download works
- [ ] Concurrent downloads setting works (1-3)
- [ ] Auto-start downloads setting works
- [ ] WiFi-only downloads setting works

## Downloads - Multi-File

- [ ] Multi-file audiobook downloads all tracks
- [ ] Tracks combine into single file
- [ ] Combined file registered as download
- [ ] Can play combined downloaded file

## Offline Mode

- [ ] App works without network (with downloads)
- [ ] Downloaded content plays offline
- [ ] Offline tabs show "(Offline)" suffix (not duplicated)
- [ ] Progress saves locally when offline
- [ ] Progress syncs to server when back online

## Streaming Cache

- [ ] Streamed files cache locally
- [ ] Cached files play without re-downloading
- [ ] Max cached files setting works
- [ ] Max cache size setting works
- [ ] "Save to Downloads" saves cached streams
- [ ] Old cache files clean up automatically

## Settings - Theme & UI

- [ ] Theme selection works (System/Light/Dark)
- [ ] Clock display toggle works
- [ ] Animations toggle works
- [ ] Debug logging toggle works

## Settings - Content Display

- [ ] Show Collections toggle works
- [ ] Show Series toggle works
- [ ] Show Authors toggle works
- [ ] Show Progress Bars toggle works
- [ ] Downloaded-Only filter works

## Settings - Playback

- [ ] Seek interval setting changes skip amount
- [ ] Resume playback toggle works
- [ ] Auto-play next chapter works (if applicable)

## Settings - Audio

- [ ] Audio quality selection works
- [ ] Volume boost toggle works
- [ ] Volume boost level adjustment works

## Settings - Downloads

- [ ] Auto-start downloads setting persists
- [ ] WiFi-only downloads setting persists
- [ ] Concurrent downloads setting persists
- [ ] Delete after finish setting works

## Settings - Account

- [ ] Username displays correctly
- [ ] Server URL displays correctly
- [ ] Logout button works

## Navigation & UI

- [ ] D-pad navigation works throughout app
- [ ] Tab switching works
- [ ] Back button returns to previous screen
- [ ] Scrolling through long lists works smoothly
- [ ] Focus indicators visible on selected items

## Error Handling

- [ ] Network errors show appropriate message
- [ ] Invalid server URL handled gracefully
- [ ] Missing content handled gracefully
- [ ] Download failures show error message

---

## Quick Smoke Test (10 minutes)

1. - [ ] Start app → Login succeeds
2. - [ ] Libraries load with covers
3. - [ ] Select audiobook → Detail view shows
4. - [ ] Play audiobook → Audio plays
5. - [ ] Seek forward/backward works
6. - [ ] Stop playback → Progress saves
7. - [ ] Download an item → Completes successfully
8. - [ ] Play downloaded item → Works offline
9. - [ ] Change a setting → Persists after restart
10. - [ ] Logout → Returns to login screen

---

## How to Use This Checklist

1. **Mark as tested**: Change `- [ ]` to `- [x]` when testing an item
2. **Mark status**: Add emoji before the item text:
   - ✓ for **working** features
   - X for **broken** features
   - Leave blank for **untested**


