---
name: Feature Testing Checklist
about: Track testing status of app features
title: "[Testing] Feature Testing Checklist"
labels: testing
assignees: ''
---

## Status Legend

| Symbol | Status |
|--------|--------|
| :heavy_check_mark: | Working |
| :x: | Broken |
| _(no symbol)_ | Untested |

---

## Authentication & Login

- [x] Login with username/password
- [x] Login credentials persist after app restart
- [x] Logout works correctly
- [x] App handles invalid credentials gracefully
- [x] App starts in offline mode when no network (with downloaded content)

## Library Browsing

- [x] All libraries from server appear as tabs
- [x] Audiobook libraries show book content
- [x] Podcast libraries show podcast content
- [x] Browse all items in a library (scroll through grid)
- [x] View collections within library
- [x] Recently added items display correctly
- [x] Item covers load properly
- [ ] Progress bars show on partially-played items
- [x] Sidebar collapse option works
- [x] Hidden libraries don't appear in sidebar

## Search

- [x] Search tab opens correctly
- [x] Search returns results from server
- [x] Search results show covers and titles
- [x] Clicking search result opens detail view

## Media Detail View

- [x] Cover image displays
- [x] Title and author display correctly
- [x] Duration shows correctly
- [x] Description/summary displays
- [x] Chapter list shows (for audiobooks with chapters)
- [ ] Chapter UI update
- [ ] Play button starts playback
- [x] Download button queues download
- [x] Delete button removes downloaded content

## Podcast Features

- [x] Podcast detail shows episode list
- [x] Episode list scrolls properly
- [x] Individual episode download works
- [x] "Find New Episodes" checks for new content
- [x] Batch download episodes works
- [x] Episode progress shows correctly

## Playback - Basic Controls

- [x] Play/Pause toggle works
- [x] Progress slider shows current position
- [x] Elapsed time displays correctly
- [x] Remaining time displays correctly
- [x] Cover art shows in player
- [x] Title and author show in player

## Playback - Seeking

- [x] Skip forward button works
- [x] Skip backward button works
- [x] Dragging progress slider seeks correctly
- [x] Seek interval setting changes skip amount (5s/10s/15s/30s/60s)

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

- [x] Progress saves when stopping playback
- [x] Progress saves periodically during playback (every 30s)
- [x] Progress syncs to server (when online)
- [x] Resume playback starts at saved position
- [x] Progress syncs FROM server on app start
- [x] Finishing content marks it as complete

## Multi-Track Audiobooks

- [ ] Multi-track books detected correctly
- [ ] Correct track downloads based on resume position
- [ ] Playback starts at correct position within track
- [ ] Remaining tracks download in background
- [ ] Background download progress shows in player
- [ ] Combined file plays seamlessly after all tracks download
