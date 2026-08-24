# hyperlight

A system audio visualiser that lives in the menu bar. It does one job.

![the strip in the menu bar](docs/menubar.png)

Twenty-eight bars, a violet-to-cyan gradient anchored to the strip rather than
to each bar, peak-hold dots that fall under gravity, and a fade to a resting row
of dots when nothing is playing. Roughly 118 × 22 points of menu bar.

## What makes this one different

**No virtual audio driver.** macOS 14.2 shipped CoreAudio process taps, and
hyperlight uses one: `CATapDescription` with an empty exclusion list gives a
global mix of everything the system is playing. There is no BlackHole to
install, no output device to switch to, and no Screen Recording permission to
grant — just a single "System Audio Recording" prompt on first launch, and the
audio path is untouched.

## Requirements

- macOS 14.2 or later (process taps landed in 14.2)
- Qt 6.11+ — the online installer, at `~/Qt/6.11.x/macos`

Windows and Linux compile but do not yet capture or display anything; see
[docs/windows-backend.md](docs/windows-backend.md) for what is decided and what
is left.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
open build/hyperlight.app
```

Qt is found automatically: `QT_ROOT` if set, otherwise the newest
`~/Qt/6.*/macos`. Override with `-DCMAKE_PREFIX_PATH=...`, or keep a local
`CMakeUserPresets.json` (git-ignored) with a preset inheriting `dev`.

Every build is code-signed with the Hypernuclear Developer ID. That is not about
distribution — an ad-hoc signature is keyed by the code hash, so each rebuild
would look like a brand new app and re-prompt for the audio permission forever.

```sh
ctest --preset dev
```

## Flags and env

| | |
|---|---|
| `--verify-audio` | Print capture levels for five seconds and exit. The fastest answer to "can it hear the system?" |
| `--windowed` | Render into a window instead of the menu bar, for iterating on the visuals |
| `--hud` | Diagnostics window alongside the menu bar item: OSStatus, callback count, RMS, AGC, sample rate, overruns |
| `--shot <path>` | Render one settled frame to a PNG and exit |
| `HYPERLIGHT_FAKE_AUDIO=1` | Drive the DSP from a synthetic sweep, so the renderer can be worked on with no audio and no permission |

## Layout

```
src/
  dsp/        FFT, band mapping, ballistics, config     -- no platform code
  audio/      lock-free ring, downmix, source interface -- no platform code
  render/     the one renderer, QPainter -> QImage      -- no platform code
  platform/   CoreAudio tap, NSStatusItem, stubs for Windows and Linux
  app/        wiring: controller, DSP thread, settings, HUD
tests/        headless, links only the shared core
```

`dsp/`, `audio/` and `render/` contain no `#ifdef Q_OS_*` and are not permitted
to. The Windows tray icon and the macOS status item are meant to show pixels
from the *same* renderer; the moment the platforms run different DSP you get to
chase "the bars look different on Windows" for a day.

## Three things that are load-bearing

**`QSystemTrayIcon` cannot do this on macOS.** Qt's Cocoa plugin calls
`setTemplate:` on the status-item image, and a template image is a monochrome
mask — the gradient would arrive as a flat silhouette. hyperlight uses a native
`NSStatusItem` hosting a layer-backed view, and never assigns
`button.image`. `QSystemTrayIcon` remains the right answer on Windows and Linux,
where nothing flattens the icon.

**The IOProc zeroes its output buffer unconditionally.** If the tap-only
aggregate ever fails and we fall back to including the default output device as
a sub-device, hyperlight becomes an output client of the user's speakers and
whatever sits in that buffer is what they hear. The HAL does not promise to hand
it over zeroed. One `memset` per callback is the cheapest insurance in the
project.

**Silence and denial look identical from the tap.** A tap without permission
delivers digital silence, and so does a machine with nothing playing. hyperlight
never concludes denial from silence alone: it cross-checks
`kAudioProcessPropertyIsRunningOutput` for an independent "something *is*
playing" signal, and it says nothing at all for the first eight seconds, because
the permission prompt is asynchronous and accusing someone of denying a
permission they have not yet been asked for is the worst thing it could do.

## Cost

Measured on an M-series Mac, Release build, 60 fps, one P-core:

| | |
|---|---|
| Visualising at 60 fps | ~7% |
| Visualising at 30 fps | ~2.9% |
| Nothing playing | display link stops; the strip holds its resting frame |

Halving the frame rate saves more than half, because most of the per-callback
cost is AppKit's display-link dispatch rather than anything hyperlight draws.
30 fps is used automatically in Low Power Mode.

## Licence

MIT. See [LICENSE](LICENSE).
