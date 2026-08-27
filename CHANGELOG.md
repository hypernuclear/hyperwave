# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- The glow flipped every frame vertically. The post-process takes its texture
  coordinate from the fullscreen triangle's vertex position, and clip space is
  y-up where an uploaded QImage is v-down, so each frame was sampled upside down
  and read back mirrored -- invisible on a symmetric look, glaring on the bars,
  whose peak dots ended up under the bars.
- `HYPERWAVE_FAKE_AUDIO` did nothing. It was parsed into an option that nothing
  read, while the README and a VS Code configuration both claimed it made
  previews behave identically on any machine. It now synthesises into the
  analyser's input, so the FFT, band mapping and ballistics run exactly as they
  do on real audio, and it is broadband rather than a handful of tones -- pure
  partials light a few bands and leave the rest at the floor, which made looks
  that depend on contrast appear broken when they were working.
- The strand and terrain settings fought back while being typed into. A spin box
  emitted on every keystroke, and each emission came round as a re-seed that
  rewrote the text under the cursor, so the field never seemed to let go. It
  commits on Enter or focus-out now, and a change is no longer echoed back to
  the panel it came from.
- Every value lost precision each time a panel was seeded. Setting a slider
  emitted valueChanged, whose handler wrote the position back through a
  thousand-step quantisation: 0.12 became 0.11998, 0.8 became 0.7981, and the
  degraded numbers were saved the next time any other control moved. A control
  being told what to show no longer answers back.

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
- Three more visualisers. "Mirror" is the bars grown from the centre line in
  both directions, moved by an underdamped spring so they overshoot and settle
  rather than easing into place. "Lines" is a wall of upright strokes lit by the
  spectrum, flashing rather than pulsing. "Ring" is a closed outline pushed
  outward by the spectrum and drawn three times in separated colours, mirrored
  so the shape stays symmetric, with the copies offset in amplitude rather than
  position so they coincide at rest and split only where the ring bulges.
- Bloom, as a post-process over whatever a look drew rather than as a property
  of any of them: a bright pass, two blurs and a composite that pulls the red
  and blue channels apart along the radius, so centre detail stays sharp and
  white while the edges split into fringes and smear outwards. Runs on the GPU
  through QRhi, sharing one device with the strands renderer, and costs about
  four points of a core -- bars measure 11.1% without it and 14.8% with.
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
- A settings window, opened from `Settings…` in the menu, with every parameter
  of the running visualiser. Values take effect on the next frame and persist
  field by field, so a store written by an older build keeps whatever it knows
  and picks up new parameters at their defaults.
- Tuned defaults for both new looks, chosen by eye rather than by argument.
  Sixty-four strands at full opacity, and a terrain of sixty-four rows with a
  column line at every sample, which is a mesh surface rather than a stack of
  ridges. Measured: bars 10.1% of a core, strands 10.7%, terrain 18.6%.
- Live controls in the debug window: the visualiser, the magnification and
  every parameter of the strands and the terrain, each taking effect on the
  next frame. They replace a flag per parameter and one process per
  combination, which is the wrong loop for anything judged by eye. Tuning is
  held in App so a geometry change cannot silently revert it.
- VS Code configurations for looking at a visualiser at a usable size.
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
