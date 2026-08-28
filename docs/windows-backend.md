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

macOS gives this app a reserved strip that no window ever covers. Windows has
no such thing on offer to a third party, so the question is which of the
imperfect substitutes to build first. Four were considered.

### The decision: an application desktop toolbar

**An appbar** -- Microsoft's name is "application desktop toolbar", and
`SHAppBarMessage` is the API -- docks a window to a screen edge and *the system
prevents other applications from using that area*. Maximised windows start below
it. That is not an approximation of the macOS menu bar, it is the same bargain:
a permanent strip of screen in exchange for never being covered.

Three things settled it.

It is the only option that is **guaranteed visible**. A floating window is
covered the moment something goes fullscreen or steals topmost; a desktop widget
is covered by the first maximised window; a taskbar overlay depends on winning a
z-order argument with a XAML shell forever.

It **solves the fullscreen problem for free**. A translucent always-on-top
window over a game in exclusive fullscreen flickers or forces the game out of
its mode, and macOS never faces this because its fullscreen is a Space. An
appbar is simply *told*: `ABN_FULLSCREENAPP` arrives when a fullscreen app opens
or the last one closes, and the documented response is to drop to the bottom of
the z-order and restore afterwards. Every other surface means hand-rolling that
detection with `SHQueryUserNotificationState` and getting the edge cases wrong.

It is **still a live API**. Deskbands -- the thing that genuinely lived *inside*
the taskbar, like the old Media Player mini-player and the Language Bar -- are
removed in Windows 11. Appbars are not: `shellapi.h`, `Shell32.lib`, supported
since XP, with an `ext-ms-win-shell-shell32` API set entry added in Windows 10.

The system also hands over `ABN_POSCHANGED` when the taskbar moves, resizes or
hides, and `ABM_GETTASKBARPOS` for its rectangle -- which every other option on
this list would have had to ask for anyway.

### The objection, which is real

**Almost nobody has knowingly seen an appbar.** The mainstream examples are
museum pieces: the Office 97 Shortcut Bar, RocketDock and ObjectDock in docked
mode. The clearest modern one is a hobby project,
[AppSwitcherBar](https://github.com/adamecr/AppSwitcherBar), which is built on
exactly this API and whose README puts the consequence plainly -- it "will
reduce the area for the other applications so they will not overlap even when
maximized".

So the convention is unfamiliar, and "this app permanently took 24 pixels of my
screen" is a confusing thing to discover rather than to choose. That is a
product problem, not a technical one, and it has product answers:

- **Never dock silently.** First run offers it, explains in one sentence that it
  reserves a strip the way the taskbar does, and defaults to *not* docked.
- **Make undocking obvious and instant.** One item in the tray menu, and
  `ABM_REMOVE` gives the space straight back.
- **Be thin.** There is no reason to take a full taskbar's height for a
  visualiser; the macOS strip is 22 points.

### The other three, kept as alternates

- **A floating strip parked over the taskbar's empty space.** The taskbar is
  already reserved area no window covers, so a topmost strip sitting in it is
  visible without costing anything extra. Attractive, and fragile: Windows 11
  centres the taskbar so the empty region moves as apps open, and the shell
  repaints on its own schedule. This is the fallback when someone does not want
  to give up the pixels.
- **A desktop widget**, Rainmeter-style, reparented under `Progman`/`WorkerW` so
  it lives on the wallpaper. Costs nothing, intrudes on nothing, and is invisible
  exactly when the machine is being used. Ship it; do not lead with it.
- **The title bar of a maximised window** was considered and rejected. Chrome,
  Edge, VS Code, Slack and Spotify all draw their own tabs, menus and search into
  that space, so the failure mode is not "looks slightly off", it is "covered the
  thing you were about to click" -- several times a day, while chasing foreground
  window geometry, per-monitor DPI and theme changes to do it.

### The tray icon is an anchor, not a visualiser

Earlier drafts of this document had the tray icon animating. It should not.

At sixteen to twenty-four pixels **none of the six looks actually reads**:
twenty-eight bars becomes eight bands of three pixels, the ring becomes a blob,
the strands become a smudge. And `Shell_NotifyIcon(NIM_MODIFY, ...)` is a
cross-process call into `explorer.exe`, so animating means an IPC round trip
twelve to fifteen times a second, forever, to draw something illegible -- behind
the Windows 11 overflow chevron, where a first run may never see it at all.

The icon's job is the menu, the settings, quitting, and being the thing a user
looks for to confirm the app is running. Draw it once.

`QSystemTrayIcon` is the right vehicle. The reason macOS needed a hand-built
`NSStatusItem` is entirely macOS's -- Qt's Cocoa plugin calls `setTemplate:` on
the status-item image and a template image is a monochrome mask, so the gradient
would arrive as a silhouette. Windows has no such behaviour.

### What the appbar means for the looks

An appbar is a strip, so all six looks work in it unchanged, which is the
opposite of the tray-icon constraint that shaped the previous draft.
`Visualiser::widthPt()` still matters, but for a different reason: the ring asks
for a square, so in a horizontal appbar it occupies its height and leaves the
rest of the bar empty. Either centre it or let the bar shrink to the look --
`setWidthPt()` already carries that decision, and on an appbar the width is ours
to choose because the *edge* is what is reserved, not the whole span.

### No display link

Either synchronise a timer to `DwmGetCompositionTimingInfo`, or accept a plain
timer and the occasional dropped frame. Note the cost table in the README before
choosing 60 over 30. `BarPresenter::setPreferredFps()` is already per-presenter,
so the appbar and the tray can run at different rates without restructuring.

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
2. **A plain frameless window** implementing `BarPresenter`. Not the shipping
   surface -- the point is to get pixels on screen and confirm the shared
   renderer, the glow and the D3D11 path all work before putting shell
   integration on top of them. Debug it here, not inside an appbar.
3. **The appbar.** `ABM_NEW`, then `ABM_QUERYPOS` followed by `ABM_SETPOS` on
   every `ABN_POSCHANGED`, then `ABN_FULLSCREENAPP`. Undocking has to work from
   the first commit: `ABM_REMOVE` before the window is destroyed is not
   optional, because an appbar that fails to unregister leaves its reserved
   strip behind after the process is gone, and the only way back is a reboot or
   another appbar claiming the same edge. Test the crash path deliberately.
4. **The static tray icon and menu**, which is what makes the app findable and
   quittable.
5. **Per-app metering**, which is cheap here and makes the `Playing` list real.

Microphone input, per-app capture, the desktop widget and the taskbar overlay
all come after that, and none of them block a first release.

## A note on building any of this

None of the Windows code is compiled anywhere today. CI builds macOS only, which
was the right call while the Windows files were stubs and the wrong one the
moment somebody writes a real backend into them.

**Add a Windows job to `.github/workflows/ci.yml` before writing the first line
of WASAPI or appbar code**, so that everything after it is at least
compile-checked by a machine that has the SDK. Writing a few hundred lines of
Win32 that has never seen a compiler is how a port acquires a hundred errors
before it acquires a single behaviour.
