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

## The notch panel

On a notched Mac the status item is not reliable. Measured on a 1728pt built-in
display with an editor's nine menus on the left and fourteen status items on the
right, hyperlight's strip gets pushed underneath the camera housing and becomes
invisible — while macOS still reports `statusItem.visible == true`. The app is
running, the tap is working, and you see nothing.

So on those Macs hyperlight also draws a panel anchored to the housing. The
space directly below the notch is the one part of that region no other app
competes for. It is additional, never a replacement: the status item is still
what people expect to find and what they can drag.

![collapsed](docs/notch-collapsed.png)

Collapsed, it is a tab hanging below the housing with the strip inside it. The
notch is 185pt wide, so the panel shows all 28 bands even when the menu bar is
too crowded for more than 14.

![expanded](docs/notch-expanded.png)

Hover and it drops down: bigger bars, what is playing, transport controls, and a
row of every app currently producing audio — click one to point the visualiser
at it.
**Right-click the panel for the menu.** That is not a convenience; it is the
only way in on the display this panel exists for. If the status item has been
crowded under the notch, "click the status item" is not an available answer.
By default the tab retracts when nothing is playing, so the notch looks stock on
a quiet machine. The window stays alive at exactly the housing's size — a
physical cutout, so nothing shows — which keeps the notch hoverable. Hovering a
retracted panel still opens it, so there is always a route to the menu and the
app list even when the status item is invisible and nothing is making noise.

## What is playing

Clicking the status item lists every application currently producing audio, by
name and icon, and lets you point the visualiser at one of them instead of the
whole system mix. Watching only Spotify means Slack pings and Zoom joins stop
jolting the bars.

Track titles and transport controls are **off by default**, behind "Track info
and controls" in the menu, because switching them on is what triggers a second
permission prompt.

That is not where this was meant to land. `MediaRemote` is the private framework
every app used for this, and on macOS 26 it still loads and every symbol still
resolves — but since 15.4 it is gated behind
`com.apple.mediaremote.send-command`, an entitlement Apple does not issue to
third parties. `MRMediaRemoteGetNowPlayingInfo` fires its callback with an empty
dictionary while music is demonstrably playing.

The fallback is AppleScript, which works but is per-application: Spotify and
Music publish scripting dictionaries, browsers do not. Audio from a YouTube tab
gets its name from the audio process list and nothing else, and the transport
buttons stay disabled rather than pretending.

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
| `--source <id>` | Listen to one app by bundle id. `--source list` prints what is available |
| `--fake-notch` | Pretend the display has a notch, for developing the panel on an external monitor |
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
