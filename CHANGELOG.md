# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-10-18

A welcome window, Open at Login, and a microphone that works on a MacBook.

### Added
- **A welcome window.** It says what hyperwave does, takes an email for the
  hypernuclear newsletter if you want it, and shows the studio's other apps. It
  opens when the app starts until you untick `Show at startup`, and
  `Welcome to hyperwave…` in the menu brings it back. Updating from 0.2.0
  shows it once on the next launch.
- **Open at Login,** on macOS (a login item) and Windows (a Startup shortcut),
  as a menu item and as a switch in the welcome window.

### Changed
- **The menu is regrouped:** the app's own items, then the three switches
  (Open at Login, Show Welcome at Startup, Send Anonymous Usage Data), then
  Quit.
- **The interface typeface is Satoshi.** Its licence does not allow it in the
  repository, so CMake downloads it from Fontshare at build time and the build
  fails if it cannot. Inter is gone.

### Fixed
- **The glow is blurred both ways now.** Its five passes shared one uniform
  block that was rewritten before each pass, but the GPU runs them all after
  the last write, so every pass read the composite's settings: both blurs ran
  vertically, the rays marched with the wrong step, and the glow's radius did
  nothing. Each pass has its own block now. Lines, Ring, and any look with the
  glow switched on will look softer and wider than before.
- **The Playing list shows its app icons on macOS 27,** which hides menu item
  images unless an app asks for them.
- **Include microphone input hears a MacBook's built-in mic.** Releases are
  signed with the hardened runtime, which silently hands an app without the
  audio-input entitlement nothing but zeros from the built-in mic -- no prompt,
  no error. USB mics were never affected, which is why it looked like one
  device's problem. Packaging now refuses to sign a release without it.
- **The microphone lands at the right pitch.** It was mixed in at its own
  sample rate rather than the analysis rate, so a MacBook Pro's mic, which runs
  at 96 kHz, showed up an octave low. It is resampled now.
- **The microphone follows the default input.** It used to stay on whichever
  device was the default when it was switched on, so connecting AirPods or
  unplugging an interface left it listening to the wrong thing, or to nothing,
  until it was toggled off and on.

## [0.2.0] - 2026-08-31

The notch panel stops being an extra and becomes a choice.

### Changed
- **On a Mac with a camera housing, the strip now draws in the notch and the
  menu bar item goes away.** 0.1.0 showed both at once. That was never a
  decision -- the panel was added as insurance against a status item being
  crowded under the housing, and nothing ever turned the insured thing off, so
  the app drew the same strip twice into the same corner of the same screen.
  Uncheck `Appearance -> Draw in the notch` to put it back in the menu bar; the
  choice is remembered and applies the moment you make it. Displays with no
  housing are unaffected and have no such entry.
- Which surface is up is now decided continuously rather than once at startup,
  because the answer changes underneath the app. Closing the lid on a docked
  MacBook takes the housing away and hands the menu bar to another screen;
  opening it hands it back.
- The expanded notch panel is the strip and nothing else. It used to carry a
  dropdown for the audio source, which was a second copy of a list the
  right-click menu already has and cost the panel a row it did not need.
- Rebuilding the per-application level meter no longer runs on the main thread.
  Enumerating processes and rebuilding the tap set are both long runs of
  synchronous calls into `coreaudiod`, and the main thread is the one drawing
  the strip.
- "About" carries a link to hypernuclear.com.
- `hyper*` product names are lowercase in everything user-visible. Storage keys
  and the signing identity keep their capitals, since changing those would
  orphan existing preferences and break the update path.

### Added
- `Appearance -> Draw in the notch`, to choose between the two surfaces.
- The panel eases open and shut on hover instead of snapping between its two
  sizes.
- A fallback for a menu bar with no room. macOS lays status items out
  right-to-left and steps straight over the camera housing, so on a crowded bar
  the slot the app is given can fall underneath it -- where there are no pixels
  and nothing to click, while `NSStatusItem.visible` still reports `YES`.
  hyperwave now measures the item against the housing and goes back to the
  panel if they overlap, holding the tab out so the panel is visible even when
  the machine is quiet. The menu entry then reads *menu bar is full*; choose it
  again once you have made room and it retries.
- Headless tests for the terrain's horizontal centring, across shear and depth
  and at both ends of its history.

### Fixed
- The terrain sat off centre, overhanging one edge of the strip and leaving
  dead space at the other. Its shear is applied so as to be centred on depth,
  but depth also sets how wide each row is: the nearest row is the widest and
  takes the whole of the shear in one direction, while the rows taking the
  other half have been narrowed by perspective. Measured at 6x on a 91pt strip,
  it overhung the left edge by 14px and left 94px empty on the right.
- Hovering the notch made the panel flicker between its two sizes instead of
  settling open. Animating a window's size makes AppKit re-evaluate its
  tracking areas on every step and emit an exit/enter pair each time, with the
  pointer stationary and inside the window throughout.
- Launching with the lid shut on a docked MacBook produced no notch panel for
  the rest of the session, and closing the lid left the panel laid out against
  a housing that was no longer there.

## [0.1.0] - 2026-08-28

First release.

### What it is

**The visualiser.** Six looks, switched in `Appearance` or with `--viz` for one
run: **Bars**, twenty-eight of them with peak-hold dots and a gradient anchored
to the strip rather than to each bar; **Mirror**, the same bars grown from the
centre line through an underdamped spring so they overshoot and settle;
**Lines**, a wall of upright strokes that flash rather than pulse; **Ring**, a
closed outline pushed outward by the spectrum and drawn five times in separated
colours; **Strands**, a bundle of lines wrapped around an axis on the GPU
through QRhi; and **Terrain**, time slices stacking away from you under
perspective.

**Colour.** Five ramps -- `signature`, `ember`, `lagoon`, `rainbow` and `mono`,
the last taking the menu bar's own foreground so it follows light and dark.
`rainbow` runs left to right on the bar-shaped looks and along depth on terrain
and strands, so the nearest ridge is the start of the ramp. Chosen in the menu
or with `--gradient`.

**Capture.** System audio on macOS through CoreAudio process taps: no virtual
audio driver, no Screen Recording permission. The source list is output devices
rather than applications, because CoreAudio will say whether a process holds an
open stream but not whether it is making a sound. A microphone or line-in can be
mixed in alongside, on by default and switchable off.

**Permission.** Silence is never treated as denial. A tap without the
system-audio grant delivers digital silence, and so does a quiet machine; the
only hard evidence of refusal is the tap failing to be created, so
"System Audio Access…" is always in the menu rather than appearing when the app
has guessed it is being denied.

**On a notched Mac**, a panel anchored under the camera housing, because a
status item on a crowded menu bar can be pushed underneath the notch while macOS
still reports it as visible. It is additional, never a replacement.

**Glow**, per look rather than one switch for everything: a bright pass, a
separable Gaussian, a radial ray march and a composite with lateral chromatic
aberration, running over whatever a look drew.

**A settings window** with every parameter of the running look, each taking
effect on the next frame and saved as it changes.

**Auto-updates**, Sparkle on macOS and WinSparkle on Windows, reading one
appcast and verifying one ed25519 signature.

**Usage analytics**, on by default and switchable off in the menu. Nothing the
app hears ever leaves the machine -- no audio, no track titles, no application
or device names. A build without an app key compiled in has no backend at all.
See "What it sends" in the README.

**Under it all**, a shared analysis chain with no platform code in it:
hand-written real FFT, geometric band mapping, ballistics, a lock-free ring, and
one QPainter renderer that every surface draws from, so the platforms cannot
drift into showing different pictures. Headless tests cover the ring, the FFT
against a naive DFT, the ballistics and the spectrum. CI fetches open-source Qt
rather than assuming one is installed.

### Added
- Usage analytics, on by default and switchable off in the menu. Nothing about
  what the app hears ever leaves the machine -- no audio, no track titles, no
  application or device names -- and a build without an app key compiled in has
  no backend at all. See "What it sends" in the README.
- A release pipeline. A `v*` tag builds macOS arm64, macOS x64 and Windows x64,
  signs and notarizes the Mac bundles, packages a DMG and an Inno installer,
  signs both feeds with one ed25519 key, and publishes to R2. Sparkle on macOS
  and WinSparkle on Windows read the same appcast and verify the same signature.
  Pull requests are built by `ci`; the release workflow runs only on a tag or a
  manual dispatch.
- An app icon on both platforms, a splash screen, and the mark and logotype as
  QML components whose colour is a property rather than baked into the artwork.

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
- The ring has glow on by default, alongside the lines. Both draw something
  that emits rather than something that reflects, which is the distinction that
  decides it.
- Microphone input is on by default, and the ring's layer opacity and the
  mirror's resting height are adjustable and persisted -- an audit of every
  field against the settings code found them tunable in principle and reachable
  by nobody.
- "Include microphone input" in the menu, which mixes an input device into the
  analysis alongside the system output so the visualiser answers "what can this
  machine hear" as well as "what is it playing". Off by default, because it
  needs its own permission and a menu bar decoration should not ask for the
  microphone on first run. A process tap cannot do this -- it answers a
  different question -- so it is a second IO proc on its own clock writing to
  its own ring, summed by the DSP over as much of each block as both have.
- Glow is per look rather than one switch for the whole app, and only the lines
  have it on by default. The bars are objects with edges and haloing them turns
  a legible meter into a smear; the lines are strokes of light and are barely
  worth drawing without it.
- The status item is sized by the look rather than by the bar style, so the ring
  asks for a square 22 x 22 points instead of sitting in the middle of a 91pt
  strip with the rest of it empty.
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
- MIT licence, and `THIRD-PARTY-NOTICES.md` covering what a
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

### Fixed
- The ring was fuzzy and drew outside itself. Its bloom ran at a quarter
  resolution, which on a 44-pixel square status item is an eleven-pixel texture
  -- blurred and composited back, that reads as being out of focus rather than
  as glow -- so the divisor now backs off on small targets. And the stroke
  width, the coloured copies' offset and the spring's overshoot were all pushing
  past the radius without being budgeted for, so a loud passage clipped against
  the top of the item.
- The microphone ran most of a second late. The output ring discards its backlog
  every tick to stay current and the input ring did not, so with two devices on
  two clocks the excess simply accumulated until the ring was full and stayed
  there. It now keeps only the block about to be mixed, which is much less than
  the output keeps: the analyser reads a whole FFT frame out of the output ring,
  while anything in the input ring beyond the current block is not early but
  late. Measured queue depth went from 65,536 frames to about 1,024 -- roughly
  680 ms to 11 ms at 96 kHz -- and the depth is now shown in the HUD and in
  `--verify-audio`, because a queue that grows is latency and should be visible
  rather than inferred.
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

### Known limits

- **Linux is stubs.** It compiles and displays nothing.
- Track title, artist and transport controls are deliberately absent. The
  plumbing they needed -- per-app AppleScript, since `MediaRemote` has been
  entitlement-gated since macOS 15.4 -- cost a permission prompt per controlled
  app and worked for two of them.

[Unreleased]: https://github.com/hypernuclear/hyperwave/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/hypernuclear/hyperwave/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/hypernuclear/hyperwave/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/hypernuclear/hyperwave/releases/tag/v0.1.0
