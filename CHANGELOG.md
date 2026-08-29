# Changelog

All notable changes to this project are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-08-28

First release.

### Added

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

### Known limits

- **Windows compiles and packages but has never been run.** WASAPI loopback, the
  application desktop toolbar and the tray icon are all written and build in CI;
  none of it has been exercised on a Windows machine. Treat it as unproven.
  See [docs/windows-backend.md](docs/windows-backend.md).
- **Linux is stubs.** It compiles and displays nothing.
- Track title, artist and transport controls are deliberately absent. The
  plumbing they needed -- per-app AppleScript, since `MediaRemote` has been
  entitlement-gated since macOS 15.4 -- cost a permission prompt per controlled
  app and worked for two of them.

[Unreleased]: https://github.com/hypernuclear/hyperwave/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/hypernuclear/hyperwave/releases/tag/v0.1.0
