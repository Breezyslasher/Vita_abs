---
name: Settings & UI Testing Checklist
about: Track testing status of settings, navigation, and error handling
title: "[Testing] Settings & UI Testing"
labels: testing
assignees: ''
---

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
