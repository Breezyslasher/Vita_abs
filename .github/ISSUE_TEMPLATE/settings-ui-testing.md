---
name: Settings & UI Testing Checklist
about: Track testing status of settings, navigation, and error handling
title: "[Testing] Settings & UI Testing"
labels: testing
assignees: ''
---

## Settings - Account

- [ ] Username displays correctly
- [ ] Server URL displays correctly (with Local/Remote indicator)
- [ ] Local URL setting editable via IME
- [ ] Remote URL setting editable via IME
- [ ] Use Server selector switches between Local/Remote
- [ ] Auto-switch when one URL is unavailable
- [ ] Logout button works with confirmation

## Settings - User Interface

- [ ] Theme selection works (System/Light/Dark)
- [ ] Show Clock toggle works
- [ ] Enable Animations toggle works
- [ ] Debug Logging toggle works

## Settings - Content Display

- [ ] Show Collections toggle works
- [ ] Show Series toggle works
- [ ] Show Authors toggle works
- [ ] Show Progress Bars toggle works
- [ ] Show Only Downloaded filter works
- [ ] Show Home Tab toggle works (requires restart)
- [ ] Max Recent Episodes selector works (5/10/15/20/Unlimited)

## Settings - Playback

- [ ] Resume Playback toggle works
- [ ] Seek Interval setting changes skip amount (5s/10s/15s/30s/60s)
- [ ] Playback Speed selector works (0.5x - 2.0x)
- [ ] Podcast Auto-Complete threshold works (Disabled/10s/30s/60s/90%/95%/99%)
- [ ] Prevent Screen Sleep toggle works
- [ ] Show Download Progress toggle works

## Settings - Audio

- [ ] Audio Quality selection works (Original/High/Medium/Low)
- [ ] Volume Boost toggle works
- [ ] Show Chapter List toggle works

## Settings - Downloads

- [ ] Auto-Start Downloads setting persists
- [ ] Download Over WiFi Only setting persists
- [ ] Max Concurrent Downloads setting persists (1/2/3)
- [ ] Delete After Finishing setting works
- [ ] Sync Progress on Connect toggle works
- [ ] Sync Progress Now button uploads to server
- [ ] Refresh Downloads List scans and updates
- [ ] Clear All Downloads with confirmation dialog
- [ ] Storage path displays correctly

## Settings - Debug

- [ ] Test Local Playback button works (ux0:data/VitaABS/test.mp3)

## Settings - About

- [ ] Version displays correctly
- [ ] App description shows
- [ ] Borealis credit shows

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
