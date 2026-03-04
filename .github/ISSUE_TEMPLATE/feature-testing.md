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

- [ ] Login with username/password
- [ ] Login credentials persist after app restart
- [ ] Logout works correctly
- [ ] App handles invalid credentials gracefully
- [ ] App starts in offline mode when no network (with downloaded content)

## Home Tab

- [ ] Home tab loads correctly
- [ ] "Continue Listening" section shows items in progress
- [ ] "Recently Added Episodes" section shows recent podcast episodes
- [ ] Clicking an audiobook in Continue Listening opens detail view
- [ ] Clicking a podcast episode starts playback directly
- [ ] Max Recent Episodes setting controls number of episodes shown

## Library Browsing

- [ ] All libraries from server appear as tabs
- [ ] Audiobook libraries show book content
- [ ] Podcast libraries show podcast content
- [ ] Browse all items in a library (scroll through grid)
- [ ] Recently added items display correctly
- [ ] Item covers load properly
- [ ] Progress bars show on partially-played items
- [ ] "Downloaded" filter button shows only downloaded items
- [ ] Collections button shows collections view
- [ ] Clicking a collection shows its contents
- [ ] Navigating back from collection returns to library

## Podcast Library Features

- [ ] "+ Find Podcasts" button opens iTunes search
- [ ] Search for podcast by name works
- [ ] Adding a podcast to server works
- [ ] "Check Episodes" button checks all podcasts for new episodes
- [ ] Auto-download of new episodes from bulk check

## Search

- [ ] Search tab opens correctly
- [ ] Search returns results from server
- [ ] Search results show covers and titles
- [ ] Results categorized by type (Books / Podcasts / Episodes)
- [ ] Clicking search result opens detail view
- [ ] Clicking podcast episode in search starts playback directly

## Media Detail View

- [ ] Cover image displays
- [ ] Title and author display correctly
- [ ] Published year displays
- [ ] Duration shows correctly
- [ ] Description/summary displays
- [ ] Chapter list shows (for audiobooks with chapters)
- [ ] Clicking a chapter starts playback from that position
- [ ] Play button starts streaming playback
- [ ] Download button queues download
- [ ] Delete button removes downloaded content

## Podcast Features

- [ ] Podcast detail shows episode list
- [ ] Episode list scrolls properly
- [ ] Individual episode download works
- [ ] Download dialog shows options (All / Unheard / Next 5 / Individual)
- [ ] "Find New Episodes" checks for new content
- [ ] New episodes dialog shows with "Download All to Server" option
- [ ] Batch download episodes works
- [ ] "Remove" button deletes all downloaded episodes
- [ ] Episode progress shows correctly

## Playback - Basic Controls

- [ ] Play/Pause toggle works
- [ ] Progress slider shows current position
- [ ] Elapsed time displays correctly
- [ ] Remaining time displays correctly
- [ ] Cover art shows in player
- [ ] Title and author show in player

## Playback - Seeking

- [ ] Skip forward button works
- [ ] Skip backward button works
- [ ] L1/R1 shoulder buttons seek backward/forward
- [ ] Dragging progress slider seeks correctly
- [ ] Seek interval setting changes skip amount (5s/10s/15s/30s/60s)

## Playback - Speed Control

- [ ] Speed button shows current speed
- [ ] Speed button cycles through speeds on click
- [ ] Can change to 0.5x speed
- [ ] Can change to 0.75x speed
- [ ] Can change to 1.0x speed (normal)
- [ ] Can change to 1.25x speed
- [ ] Can change to 1.5x speed
- [ ] Can change to 1.75x speed
- [ ] Can change to 2.0x speed
- [ ] Speed persists when changing tracks

## Playback - Streaming

- [ ] Stream audiobook directly from server (no download)
- [ ] Stream podcast episode directly from server
- [ ] Streaming works over HTTP URL
- [ ] Buffering indicator shows when loading

## Playback - Progress & Resume

- [ ] Progress saves when stopping playback
- [ ] Progress saves periodically during playback (every 30s)
- [ ] Progress syncs to server (when online)
- [ ] Resume playback starts at saved position
- [ ] Progress syncs FROM server on app start
- [ ] Finishing content marks it as complete
- [ ] Podcast auto-complete marks episode finished near end

## Multi-Track Audiobooks

- [ ] Multi-track books detected correctly
- [ ] Correct track downloads based on resume position
- [ ] Playback starts at correct position within track
- [ ] Remaining tracks download in background
- [ ] Background download progress shows in player
- [ ] Combined file plays seamlessly after all tracks download
