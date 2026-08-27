# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- `--windowed` segfaulted on launch. The windowed surface has no presenter, and
  `rebuildStyle()` dereferenced one unconditionally to ask whether it preferred
  a compact strip — so the mode documented as the way to iterate on the visuals,
  and committed as a VS Code configuration, crashed before drawing a frame.

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
- A third visualiser, "Strands": a bundle of lines wrapped around an axis with
  the spectrum and three octaves of simplex noise driving the radius, tapered to
  points at both ends. A port of Matt DesLauriers' codevember #21 (MIT, credited
  in THIRD-PARTY-NOTICES.md), on the GPU through QRhi and Metal because the
  geometry is static and every frame's motion is a vertex shader. It costs less
  than the bars do -- 10.8% of a core against 12.0% -- and the strand count is
  effectively free, with 32 and 1024 measuring the same, because the whole CPU
  cost is the fixed offscreen round trip. Settable with HYPERWAVE_STRANDS.
- Bars a point apart instead of two, as tight as they go while every edge still
  lands on a whole device pixel at 1x, 2x and 3x. The strip narrows from 118 to
  91 points as a result.
- VS Code configurations for looking at a visualiser at a usable size, one of
  which asks for the look, the magnification and the strand count at launch.
- `--preview-scale`, which magnifies the windowed and `--shot` canvas so a
  visualiser can be looked at where its detail is visible rather than at
  118 x 22 points.
- A second visualiser, "Terrain": a spectrogram ridgeline with frequency across,
  level up and time receding under perspective, chosen in `Appearance` or with
  `--viz ridge`. Projected in software rather than on the GPU, because at status
  item and notch panel sizes the whole terrain draws in 1.0 ms and a GPU path
  would need a context, a framebuffer and a readback per surface to get back to
  the QImage every presenter here requires. Qt Quick 3D was ruled out separately:
  it is GPLv3-only and would change what may be shipped.
- A `Visualiser` interface, so a look is a class rather than a branch, and every
  surface holds one instead of a concrete renderer.
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
