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
grant — just a single "System Audio Recording" grant, and the audio path is
untouched.

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
dropdown for the audio source.

**Right-click the panel for the menu.** That is not a convenience; it is the
only way in on the display this panel exists for. If the status item has been
crowded under the notch, "click the status item" is not an available answer.

By default the tab retracts when nothing is playing, so the notch looks stock on
a quiet machine. The window stays alive at exactly the housing's size — a
physical cutout, so nothing shows — which keeps the notch hoverable. Hovering a
retracted panel still opens it, so there is always a route to the menu even when
the status item is invisible and nothing is making noise.

## Choosing a source

The source list is **output devices**, not applications.

Applications were the wrong axis twice over. CoreAudio will tell you whether a
process holds an open output stream — `kAudioProcessPropertyIsRunningOutput` —
but not whether it is actually making a sound, and there is no property that
does. Measured on an idle machine, a 3D editor and a paused music player both
reported "playing" with the room silent. And a Mac with a virtual device, an
audio interface and three monitors has a genuinely interesting choice of *where*
sound is going, which an app list cannot express at all.

So: `Source ▸` in the menu, and a dropdown in the notch panel, both listing
every device with output channels and marking the current one. Picking one taps
`CATapDescription initExcludingProcesses:andDeviceUID:withStream:` — still a
whole-system mix, restricted to one destination. Verified by tapping the
built-in speakers while audio was routed to a virtual device: the tap stayed
alive at 463 callbacks and reported −120 dB, against −14.2 dB for the device
actually in use.

## What is playing

`Playing ▸` lists the applications actually making a sound, loudest first, with
their icons. Informational and deliberately not selectable: which apps have
audio going is a different question from where the visualiser listens.

The list is built from a **measurement**, not from
`kAudioProcessPropertyIsRunningOutput`, and it is grouped by application rather
than by process. Both halves were necessary and each was learned the hard way.
That property is wrong in both directions: a 3D editor holds a stream open for
hours while emitting digital silence, and a browser plays through a helper that
never claims one at all. It is not consulted here any more, not even to order
candidates — Apple's background machinery holds output streams open
permanently, so ranking stream-holders first handed every tap slot to
`universalaccessd` and `loginwindow` and pushed real applications off the end.
Grouping matters just as much, because the level and the identity are almost
never on the same process. Measured with a YouTube tab playing: a tap on the
*unnamed* `com.google.Chrome.helper` read **−17.0 dB**, taps on
`com.google.Chrome` and on the named "Google Chrome Helper" both read −120, and
a global tap minus Spotify read −16.9 — the same signal. Chrome's audio was
fully attributable the whole time, just not to a process anyone would
recognise. So every process an application owns is metered, the loudest wins,
and the name and icon come from whichever process has them.
Some applications have no named process at all — Electron apps like Cursor
register three helpers and never themselves — so the name is resolved from the
bundle id through LaunchServices. Anything that resolves to nothing is
anonymous by nature, cannot appear in a menu, and is metered last if at all:
`systemsoundserverd`, WebKit's shared GPU service, the Core Audio driver host.
That last one is worth naming, because it is the loudest decoy on the machine —
when a virtual output device is in use it re-renders the entire system mix at
about −41 dB and looks, to a naive meter, like the answer.
The measurement is cheap because one private aggregate device can hold *several*
taps at once: `kAudioAggregateDeviceTapListKey` takes an array, and each tap
arrives as its own buffer in a single IOProc. So metering N applications costs
one aggregate, one IO thread, and an RMS per buffer — not N of everything. With
music playing and an editor idle, the editor's own tap reads −120 dB while the
player's reads −5.9 dB, which is not a close call.

The level decays over about two seconds rather than tracking the signal, so a
gap between tracks does not make an app blink out of the list. A scriptable app
that reports itself paused overrides the meter, because it knows better.
Verified end to end, with `HYPERLIGHT_METER_DEBUG=1`: silent machine with a 3D
editor and a paused player running, nothing listed; YouTube playing in Chrome,
`Google Chrome −21.3 dB`; tab stopped, the level decaying away through −33 and
−42 to nothing.

## Track info

Off by default, behind "Track info and controls", because switching it on is
what triggers a second permission prompt. When on, the track, artist and
transport appear in the menu **and** in the notch panel — the panel is one
consumer of it, not the reason for it.

That is not where this was meant to land. `MediaRemote` is the private framework
every app used for this, and on macOS 26 it still loads and every symbol still
resolves — but since 15.4 it is gated behind
`com.apple.mediaremote.send-command`, an entitlement Apple does not issue to
third parties. `MRMediaRemoteGetNowPlayingInfo` fires its callback with an empty
dictionary while music is demonstrably playing.

The fallback is AppleScript, which works but is per-application: Spotify and
Music publish scripting dictionaries, browsers do not. Anything else shows its
name and leaves the transport disabled rather than pretending.

## Permission

The system-audio grant is **required and silently enforced**. Without it
`AudioHardwareCreateProcessTap` still succeeds, the IOProc still fires, and
every sample is zero. Verified by running the same signed binary at the same
moment under two bundle ids: the granted one read −14.2 dB, the ungranted one
−120 dB with the tap alive at 39 callbacks. macOS did not prompt.

Two things follow.

**"System Audio Access…" is always in the menu**, never conditional. It used to
appear only when the app had concluded it was being denied — worse than useless,
because the conclusion was unreliable and because macOS may never prompt on its
own. Silence is now treated as silence; the only hard evidence of denial is the
tap creation failing outright.

**Running from a terminal masks all of this.** TCC attributes a capture request
to the *responsible* process, and a binary exec'd from a shell inherits the
terminal's grant rather than using its own. So running the executable directly
works even with no grant of its own — and hyperlight will not appear in System
Settings, because the permission in play belongs to the terminal. To exercise
the real path, launch through LaunchServices:

```sh
open build/hyperlight.app
```

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

### In VS Code

`.vscode/` is committed and carries no machine-specific paths. F5 gives you the
menu bar item; the drop-down also has windowed mode for renderer work, a HUD
configuration, a faked notch for developing the panel in clamshell, a
capture-only run, and one entry per test binary for stepping into a failure.

One wrinkle worth knowing before it costs you an afternoon: a debugger launches
hyperlight as a child of VS Code, so **TCC uses VS Code's system-audio grant,
not hyperlight's**. If VS Code has not been granted "System Audio Recording",
the tap is created, the IOProc fires, and every sample is zero — the app looks
alive and the bars stay flat. There is a "Run as a real app (LaunchServices)"
task for checking the other side of that.

## Flags and env

| | |
|---|---|
| `--verify-audio` | Print capture levels for five seconds and exit. The fastest answer to "can it hear the system?" |
| `--windowed` | Render into a window instead of the menu bar, for iterating on the visuals |
| `--hud` | Diagnostics window: OSStatus, callback count, RMS, AGC, sample rate, overruns |
| `--shot <path>` | Render one settled frame to a PNG and exit |
| `--source <uid>` | Listen to one output device by UID. `--source list` prints them |
| `--fake-notch` | Pretend the display has a notch, for developing the panel on an external monitor |
| `HYPERLIGHT_FAKE_AUDIO=1` | Drive the DSP from a synthetic sweep, so the renderer can be worked on with no audio and no permission |
| `HYPERLIGHT_METER_DEBUG=1` | Log the metered applications in priority order, any that ran out of tap slots, and the "Playing" list itself every two seconds. Every wrong answer this list has given was found by a human opening a menu and disagreeing with it; this makes the same question answerable in one command |

## Layout

```
src/
  dsp/        FFT, band mapping, ballistics, config     -- no platform code
  audio/      lock-free ring, downmix, source interface -- no platform code
  render/     the one renderer, QPainter -> QImage      -- no platform code
  platform/   CoreAudio tap, NSStatusItem, notch panel, stubs for Windows/Linux
  app/        wiring: controller, DSP thread, settings, menu model, HUD
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
`NSStatusItem` hosting a layer-backed view, and never assigns `button.image`.
`QSystemTrayIcon` remains the right answer on Windows and Linux, where nothing
flattens the icon.

**The IOProc zeroes its output buffer unconditionally.** If the tap-only
aggregate ever fails and we fall back to including the default output device as
a sub-device, hyperlight becomes an output client of the user's speakers and
whatever sits in that buffer is what they hear. The HAL does not promise to hand
it over zeroed. One `memset` per callback is the cheapest insurance in the
project.

**Silence is never treated as denial.** A tap without permission delivers
digital silence, and so does a machine with nothing playing, and CoreAudio
exposes no per-process level that could tell them apart. The original design
thought `IsRunningOutput` could; it cannot, and the resulting probe accused a
working install on any quiet desktop.

## Cost

Measured on an M-series Mac, Release build, 60 fps, one P-core:

| | |
|---|---|
| Visualising at 60 fps | ~9% |
| Visualising at 30 fps | ~4% |
| Nothing playing | display link stops; the strip holds its resting frame |
About 2 points of that is the per-application metering. Forty live taps cost no
more than twenty did — measured at 9.1% against the same 9% — because the
metering IOProc itself is free (8 samples in a 4-second profile) and the HAL is
already delivering the streams. Forty is enough to cover every third-party
application on a busy machine with its helpers, which is the number that
matters: an app that loses a tap slot is invisible, and looks exactly like a
bug in the meter. Turning the source picker's `--source list` on a quiet machine
is the cheapest way to see the same information without it.

Halving the frame rate saves more than half, because most of the per-callback
cost is AppKit's display-link dispatch rather than anything hyperlight draws.
30 fps is used automatically in Low Power Mode.

## Licence

MIT. See [LICENSE](LICENSE).
