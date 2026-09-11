# Hyperwave Studio

A separate windowed app for making marketing footage with Hyperwave's actual
renderers. Cycle through Bars, Mirror, Lines, Ring, Strands, and Terrain, tune
their production controls, then save a transparent still or a PNG sequence.

## Launch from VS Code

Select the **dev** or **relwithdebinfo** CMake configure preset. In **Run and
Debug**, select **hyperwave: Studio (PNG capture)** and press **F5**. The launch
task enables Studio in the current build directory and builds the
`hyperwave_studio` target.

Studio needs Qt **Multimedia**, in addition to the main app's Qt modules. Add
it through the Qt Maintenance Tool if it is missing. Studio is enabled in the
development presets and excluded from the release and CI presets. An existing
custom build can enable it with `-DHYPERWAVE_BUILD_STUDIO=ON`.

```sh
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo --target hyperwave_studio
open build/hyperwave_studio.app
```

Studio has its own app identifier and settings. It does not install a status
item, run analytics, or change the main app's appearance. System audio capture
starts only when you select that source, and stops when you switch away or close
Studio. The microphone is excluded.

## Prepare a take

1. Use the arrows beneath the preview to cycle visualizers. **Look** contains
   the same tuning controls as Hyperwave. Each look keeps its tuning while the
   window is open; reset restores that look's defaults.
2. Select **Menu bar**, **Notch · collapsed**, or **Notch · expanded**. The notch
   presets use the same 185 pt housing and layout values as `--fake-notch`.
   **Include black notch background** exports the shell along with the strip.
   Leave it off to composite your own shell in After Effects. The transparent
   housing region stays in the canvas so the strip remains registered to it.
3. Enter the output width and height in **pixels**. Matching the layout aspect
   ratio updates the other dimension. Unlock it to use any canvas ratio; the
   visualizer fits proportionally in the center, with transparent padding.
   Limits are 16–8192 px per side and 16 megapixels total.
4. Choose **Demo audio**, **Audio file**, or **System audio** in the source menu.
   **Open audio…** loads a soundtrack.
   WAV, AIFF, MP3, M4A, FLAC, and Ogg are offered; actual decoding support depends
   on the installed Qt multimedia backend. WAV is a useful fallback. Audio is
   decoded into memory, with a ten-minute limit. There is no system-audio grant
   needed for file input. The demo drives motion but is not played aloud.
5. Set the start and duration. Durations are rounded to whole frames at 60 fps;
   the exact frame count and resulting duration appear below the controls.
   The selected range must fit within file audio. Preview loops this range.
   System audio previews live and records from now, so its start control is disabled.

### System audio

Select **System audio** to visualize what your computer is playing. On macOS,
use **System Audio Access…** if the preview stays quiet while music is playing.
When debugging from VS Code, grant **Visual Studio Code** access to **System
Audio Recording** in System Settings. When launched through Finder or `open`,
the grant belongs to **Hyperwave Studio**. Click **Retry capture** after changing
the grant; a relaunch may be needed. Silence by itself is not proof of a denied
grant. Windows uses the existing WASAPI loopback backend; Linux remains unsupported.

**Record & export…** first records the selected duration, starting after you
choose the output folder. A dedicated thread captures audio while the preview
runs. It then renders every PNG offline at 60 fps, so an expensive visualizer
cannot change the export's playback speed. The take includes **audio.wav**, the
mono system mix as PCM16, trimmed to align with frame zero. Place it alongside
the sequence in After Effects without an additional offset. The two-second
analysis pre-roll is excluded from this WAV.

**Cancel recording** discards the in-memory take before export. During PNG
rendering, cancellation keeps completed files as usual. A device/rate change,
capture overrun, or stopped audio stream aborts recording with a message rather
than silently exporting a take with missing audio.

The checkerboard, black, and light **preview backgrounds** never appear in the
export. **Save frame…** saves the current full-resolution frame, including its
alpha channel; pause to pick a frame before saving it.

## Export and composite

**Export PNG sequence…** asks for a parent folder and creates a fresh, uniquely
named take inside it. Existing takes are never overwritten. Each take contains:

- `frame_000000.png`, `frame_000001.png`, … at the requested pixel dimensions.
- `capture.json`, recording the frame rate, dimensions, audio start time,
  source, layout, palette, alpha interpretation, and completion status.
- For system audio, `audio.wav`, aligned with the first frame. The manifest's
  `audioFileStartSeconds` is zero for this file.

Exports use **60 fps**, **straight alpha**, and tagged **sRGB**. Each frame is
rendered directly at the output resolution. Frame time advances independently
of wall time, including Strands' shader animation: a slow export still produces
all frames at the correct playback speed. Two seconds of pre-roll settle the
DSP and renderer history; audio-file pre-roll uses preceding samples, or silence
before the file starts. Pre-roll frames are not saved.

The interface remains available between frames. **Cancel export** keeps the
completed PNGs and marks the take incomplete. Closing the window during export
does the same. Write errors are reported and the take is not marked complete.

In After Effects, import the first PNG with **PNG Sequence** enabled. Under
**Interpret Footage → Main**, set **60 fps** and **Straight (Unmatted)** alpha.
Interpret the embedded sRGB color profile consistently with your composition.
For file audio, add your soundtrack separately, trimmed to `startSeconds` in
`capture.json`. For system audio, add the included `audio.wav` without trimming.
PNG sequences themselves contain no audio. Recreate the menu bar or notch in separate
layers, or keep the exported black notch shell.

Studio exports fixed layout takes. It does not capture the macOS menu bar,
pointer, or the notch's hover transition. Those can be composed in After Effects
or recorded from the running app for reference. The preview aims for real time;
heavy looks at large resolutions may preview more slowly. Offline frame timing
is unaffected.

## Repeatable command-line takes

The same exporter is also available without opening the window:

```sh
mkdir -p /tmp/hyperwave-takes
build/hyperwave_studio.app/Contents/MacOS/hyperwave_studio \
  --export /tmp/hyperwave-takes --viz bars --gradient signature \
  --layout notch-expanded --size 1508x424 --frames 600
```

Add `--panel` for the notch shell, `--compact` for a compact menu-bar strip, or
`--audio /path/to/music.wav --start 15` for a soundtrack. `--help` lists all flags.
Use `--system-audio` to record system output for the requested frame duration
before export. It cannot be combined with `--audio` or `--start`. Without
`--export`, `--system-audio` opens the window with the live source selected.

## Verification

```sh
cmake --build build --target tst_studio
ctest --test-dir build -R tst_studio --output-on-failure
```

The tests exercise all renderers and layouts, exact output size, transparency,
PNG color/alpha round trips, independent frame timing, audio-file decoding,
sequence numbering and length, cancellation, and error reporting. System audio
tests use an injected capture source to verify recording continues when the UI
is blocked, sample ordering and WAV alignment are preserved, and interrupted
takes are rejected.
