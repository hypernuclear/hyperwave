# Windows and Linux

Nothing here is implemented. This records what was decided, and why, so whoever
picks it up does not have to re-litigate it.

The seams are already cut: `AudioSource` (`src/audio/AudioSource.h`) and
`BarPresenter` (`src/platform/BarPresenter.h`) are the only two interfaces that
need per-platform implementations. Everything between them — the ring, the FFT,
the band mapping, the ballistics, the renderer — is shared and already works.

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

Both surfaces render from `BarRenderer`, unchanged. The tray path converts the
`QImage` to an `HICON` at whatever size the shell asks for — typically 16 or 20
logical pixels, so roughly eight bands rather than twenty-eight. `RenderStyle`
already carries `bandCount` for exactly this; nothing else needs to change.

Two things that will bite:

- Windows 11 hides new tray icons behind the overflow chevron by default, so a
  first run can look like nothing happened. **The strip is what makes the app
  discoverable**; the tray icon is what makes it feel native. Do not ship only
  the icon.
- There is no display link. Either synchronise a timer to
  `DwmGetCompositionTimingInfo`, or accept a plain 60 Hz timer and the
  occasional dropped frame. Note the macOS measurement in the README before
  choosing 60 over 30.

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
`SpectrumAnalyzer` currently infers silence from the samples it is given.

## Linux

`QSystemTrayIcon` is the right vehicle, and unlike macOS it is not
booby-trapped: the XCB and Wayland backends publish a StatusNotifierItem and
pass the icon through without template flattening, so the gradient survives. KDE
and most Qt desktops show it directly. **GNOME needs the AppIndicator
extension** — say so in the README rather than letting people discover it.

The frameless strip from the Windows plan is the fallback where there is no
usable tray, and it is the same code either way.

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
