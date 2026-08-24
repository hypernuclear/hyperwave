# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- System audio capture on macOS via CoreAudio process taps, with no virtual
  audio driver and no Screen Recording permission. Probes for a tap-only
  aggregate device first, and only falls back to including the default output
  device if that produces no input stream.
- A native `NSStatusItem` hosting a layer-backed view, driven by a
  `CADisplayLink`. Deliberately not `QSystemTrayIcon`: Qt's Cocoa plugin calls
  `setTemplate:` on the status-item image, which would flatten the gradient to
  a monochrome silhouette.
- Shared, platform-free analysis chain: hand-written real FFT, geometrically
  spaced band mapping, attack/release ballistics, gravity-driven peak-hold dots
  and a slow AGC.
- One shared `QPainter` renderer, so the eventual Windows tray icon and Linux
  StatusNotifierItem draw the same pixels.
- Two-signal permission detection: silence from the tap is cross-checked
  against `kAudioProcessPropertyIsRunningOutput` before concluding denial, with
  an eight-second grace window for the asynchronous TCC prompt.
- Display link sleeps once the picture stops moving, and drops to 30 fps in Low
  Power Mode.
- Compiling stubs for WASAPI loopback, PulseAudio monitor capture, and the
  Windows and Linux presenters, each documenting the chosen approach.
- Headless tests for the ring buffer, the FFT (against a naive DFT), the
  spectrum analyser and the ballistics.

[Unreleased]: https://github.com/hypernuclear/hyperlight/commits/main
