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
- Permission handling that never treats silence as denial. A tap without the
  grant delivers digital silence, and so does a quiet machine; the only hard
  evidence of denial is the tap failing to be created, so "System Audio
  Access…" is always in the menu rather than appearing when the app has decided
  it is being refused.
- Display link sleeps once the picture stops moving, and drops to 30 fps in Low
  Power Mode.
- Compiling stubs for WASAPI loopback, PulseAudio monitor capture, and the
  Windows and Linux presenters, each documenting the chosen approach.
- Headless tests for the ring buffer, the FFT (against a naive DFT), the
  spectrum analyser and the ballistics.
- MIT licence, matching hyperbin, and `THIRD-PARTY-NOTICES.md` covering what a
  build actually links. Every Qt module's terms were read out of the
  `SPDX-License-Identifier` in its own installed headers rather than from the
  documentation: everything hyperwave uses offers LGPLv3, and Qt Quick 3D —
  anticipated but not yet linked — is `LicenseRef-Qt-Commercial OR
  GPL-3.0-only`, with no LGPL option, which would make an open-source-Qt binary
  GPLv3 the moment it is added.
- CI that fetches open-source Qt rather than assuming one is installed:
  `install-qt-action` with aqtinstall pinned to a commit that understands Qt
  6.11's repository layout, building and testing arm64 and x86_64 on every push
  and pull request.

[Unreleased]: https://github.com/hypernuclear/hyperwave/commits/main
