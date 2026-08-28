# Windows and Linux

Nothing here is implemented. This records what was decided, and why, so whoever
picks it up does not have to re-litigate it.

The seams are already cut: `AudioSource` (`src/audio/AudioSource.h`) and
`BarPresenter` (`src/platform/BarPresenter.h`) are the only two interfaces that
need per-platform implementations. Everything between them — the ring, the FFT,
the band mapping, the ballistics, the six renderers, the glow — is shared and
already works.

---

## Windows: where the visualiser lives

Three options were considered and two are dead:

- **Deskbands**, the classic "toolbar docked inside the taskbar", are the
  obvious answer and are **removed in Windows 11**. Not deprecated — removed.
- **Windows 11 Widgets** are Adaptive Cards with a refresh budget measured in
  minutes. They cannot animate, so they cannot host a visualiser.
- **An AppBar** (`SHAppBarMessage`) does work: it docks a real strip to a screen
  edge that maximised windows respect. It also permanently reserves screen
  space, which is a heavy price for an ambient decoration. Kept in reserve.

**The decision: an animated notification-area icon as the always-present anchor,
plus an optional frameless, translucent strip that snaps just above the taskbar
and remembers where it was dragged.** Ship both, let the user pick.

### The tray is square, and that is now a real distinction

This is the part that changed since the decision was made, and it changed for
the better.

`Visualiser::widthPt()` splits the looks into two families. The ring asks for a
square; the bars, mirror, lines, terrain and strands ask for a strip. That was
added so a circle would not sit in the middle of ninety-one points of empty menu
bar — but a notification-area icon is *inherently* square, sixteen to thirty-two
pixels depending on DPI, and the split turns out to describe Windows better than
it describes macOS.

So: **the ring is the tray look, and the strip looks go on the strip.**
Twenty-eight bars in a 24-pixel icon is eight bands of three pixels, which is a
smudge. A ring in a 24-pixel icon is a ring. Nothing needs inventing for this;
the taxonomy is already in the code.

### Frame rate in the tray is a limit, not a preference

`Shell_NotifyIcon(NIM_MODIFY, …)` is a cross-process call into `explorer.exe`.
Doing it sixty times a second, forever, is antisocial in a way that a
`CADisplayLink` on your own layer is not. Twelve to fifteen is the honest
ceiling.

`BarPresenter::setPreferredFps()` is already per-presenter, so this needs no
restructuring — the tray presenter simply asks for less than the strip does. But
it does change which looks survive there: peak-hold dots and the mirror's spring
read fine at fifteen, while the strands' spin and the lines' flash will strobe.
Another argument for the ring.

### Two things that will not bite, and one that will

**`QSystemTrayIcon` is fine on Windows.** The reason macOS needed a hand-built
`NSStatusItem` is entirely macOS's: Qt's Cocoa plugin calls `setTemplate:` on the
status-item image, and a template image is a monochrome mask, so the gradient
would arrive as a silhouette. Windows has no such behaviour. Build a `QIcon`
from the `QImage` and let Qt do the `HICON` conversion; do not hand-roll
`CreateIconIndirect`.

**The glow already has a Windows path.** `GpuContext` has the
`QRhiD3D11InitParams` branch, and the shaders bake HLSL 5.0 alongside MSL and
SPIR-V — verified with `qsb --dump`, not assumed. The bloom divisor also already
backs off on small targets, which is exactly the tray case: a 24-pixel icon gets
a full-resolution bloom rather than a six-pixel one.

**Discoverability will bite.** Windows 11 hides new tray icons behind the
overflow chevron by default, so a first run can look like nothing happened. The
strip is what makes the app discoverable; the icon is what makes it feel native.
Do not ship only the icon.

### No display link

Either synchronise a timer to `DwmGetCompositionTimingInfo`, or accept a plain
timer and the occasional dropped frame. Note the cost table in the README before
choosing 60 over 30.

## Windows: what is markedly easier here

**Per-application metering.** The single hardest thing on the macOS side — one
private aggregate holding a tap per process, built because no CoreAudio property
will answer "is this application making a sound" — is nearly free on Windows.
`IAudioSessionManager2::GetSessionEnumerator` gives the sessions,
`IAudioSessionControl2` gives each one's PID and display name, and
`IAudioMeterInformation::GetPeakValue` gives its level. No taps, no aggregate,
no permission, no forty-slot budget. Expect the `Playing` list to be an
afternoon rather than the week it took here.

Icons come from the same place: `GetProcessId` → `QueryFullProcessImageName` →
`SHGetFileInfo`.

**Listening to one application.** Windows 10 2004 added process loopback via
`ActivateAudioInterfaceAsync` with `AUDIOCLIENT_ACTIVATION_PARAMS` and
`PROCESS_LOOPBACK`, which is the direct analogue of a per-process tap. The
`Source` picker gets "one app" as well as "one device" without new concepts.

## Windows: what is harder, and has no macOS equivalent

**Exclusive fullscreen.** A translucent, always-on-top window sitting over a game
in exclusive fullscreen either flickers, gets composited away, or forces the game
out of the mode. macOS fullscreen is a Space and none of this arises.
`SHQueryUserNotificationState` reports `QUNS_RUNNING_D3D_FULL_SCREEN` and
`QUNS_PRESENTATION_MODE`; the strip has to hide itself for both and come back
afterwards. This is the difference between a decoration and a thing people
uninstall.

**Placement.** Windows 11 centres the taskbar, so the strip's default position
should be centred above it rather than left-aligned. `ABM_GETTASKBARPOS` gives
the edge and the auto-hide state — the taskbar is not always at the bottom and
not always present. Remember which *monitor* the strip was on rather than only
its coordinates: with per-monitor DPI the same point means different things on
different screens.

**Window flags.** `WS_EX_NOACTIVATE` so clicking the strip does not steal focus,
`WS_EX_TOOLWINDOW` to keep it out of Alt-Tab, layered for per-pixel alpha. In Qt
terms: `Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint` with
`WA_TranslucentBackground`, plus the native flags for the two there is no Qt
spelling of.

## Windows: capture

WASAPI loopback, which is the direct analogue of the macOS process tap and
markedly simpler: activate the default render endpoint with `IAudioClient`,
initialise with `AUDCLNT_STREAMFLAGS_LOOPBACK`, read packets off
`IAudioCaptureClient`. No permission prompt, no virtual device, no aggregate.

Reuse `downmixInterleaved` and `sanitize` from `audio/Downmix.h` — WASAPI hands
back interleaved float32 in the mix format, so the existing path applies
verbatim. Reuse `MonoRing` unchanged, with the capture thread as producer.

Watch `IMMNotificationClient::OnDefaultDeviceChanged` for device changes, bump
`formatEpoch`, and **debounce it by the same 250 ms the macOS side uses**.
Notification storms on device connect are not a macOS-specific phenomenon.

One difference with no macOS equivalent, and it is easy to miss: **a loopback
stream delivers nothing at all while the endpoint is idle**, rather than
delivering silence. The DSP will simply see no samples arriving. Silence
detection therefore has to key off "no packets for N ms" as well as off RMS —
`SpectrumAnalyzer` currently infers silence from the samples it is given. This
matters more than it did when it was first written: the display clock now sleeps
on silence, so getting it wrong means the picture freezes rather than fades.

**Microphone input** is a second `IAudioClient` on a capture endpoint, into its
own ring, summed by `DspWorker` exactly as on macOS. Two notes carry over
unchanged. Desktop applications have been subject to the microphone privacy
setting since Windows 10 1903, and a denied one produces silence rather than an
error — the same shape as the macOS grant, so the rule that silence is never
treated as denial ports verbatim. And the input ring must discard its backlog
every tick; two devices on two clocks drift on Windows for the same reason they
drift here.

## Linux

`QSystemTrayIcon` is the right vehicle, and unlike macOS it is not
booby-trapped: the XCB and Wayland backends publish a StatusNotifierItem and
pass the icon through without template flattening, so the gradient survives. KDE
and most Qt desktops show it directly. **GNOME needs the AppIndicator
extension** — say so in the README rather than letting people discover it.

The same square-versus-strip split applies: a StatusNotifierItem is an icon, so
the ring belongs there and the strip looks belong on the frameless window.

The frameless strip from the Windows plan is the fallback where there is no
usable tray, and it is the same code either way. Wayland will not let a client
position its own window, so "snap above the taskbar" is a wish rather than an
instruction there; `wlr-layer-shell` is the protocol that grants it, and not
every compositor implements it.

Capture: libpulse's simple API against the default sink's monitor source
(`<sink>.monitor`). That works on PulseAudio and, through the pulse shim, on
PipeWire as well — essentially every desktop, for a fraction of the code a
native PipeWire client would take. Native libpipewire is the upgrade path if the
shim proves lacking, not the starting point. Request `PA_SAMPLE_FLOAT32NE` so no
conversion is needed.

The sharp edge: the default sink can change underneath you and monitor sources
come and go with it. Subscribe to `PA_SUBSCRIPTION_MASK_SERVER`, reconnect, bump
`formatEpoch` — the same shape as the macOS device-change path.

No display link here either, but `QScreen::refreshRate` is enough to pick a
sensible timer interval.

## The order to build it in

1. **WASAPI loopback into `MonoRing`.** Everything else is invisible until
   samples arrive, and `--verify-audio` already proves it end to end without a
   single pixel being drawn.
2. **The frameless strip.** It is the discoverable surface, it has no size
   constraint, and all six looks work on it unchanged. This is the point at
   which the app is usable on Windows.
3. **The tray icon**, square, ring by default, at fifteen frames a second.
4. **Per-app metering**, which is cheap here and makes the `Playing` list real.
5. **Fullscreen detection**, before someone with a game finds it first.

Microphone input, per-app capture and the AppBar come after that, and none of
them block a first release.
