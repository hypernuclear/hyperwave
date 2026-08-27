# Third-party notices

hyperwave's own source is MIT — see `LICENSE`. This file covers everything
else that ends up in, or is linked by, a build.

Every claim below was checked against the licence text actually present in the
dependency rather than from memory. For Qt that means the `SPDX-License-Identifier`
line in the installed module's own headers, read out of `Qt/6.11.2/macos/lib/`,
not the module documentation pages.

## Qt 6 — the one that decides what you may ship

**Qt is the only dependency whose terms constrain the finished binary**, and
which terms apply depends on **how the Qt you linked was obtained**, not on what
this repository says.

| Module | SPDX identifier in the shipped headers |
|---|---|
| Core, Gui, Widgets, Svg, Qml, Quick, Quick Controls 2, Test | `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only` |
| Shader Tools (runtime library) | `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only` |
| **Quick 3D** | **`LicenseRef-Qt-Commercial OR GPL-3.0-only`** |

Two consequences worth stating plainly:

* **As hyperwave is built today** — Core, Gui, Widgets, Svg, Qml, Quick, Quick
  Controls 2, Test — every linked module offers LGPLv3, so a binary built
  against open-source Qt may be distributed under LGPLv3's terms: dynamic
  linking against unmodified Qt, and users able to relink against their own Qt.
  The app links Qt's frameworks dynamically and does not modify Qt.
* **Built against commercially licensed Qt**, that obligation does not arise and
  the binary may be distributed on whatever terms you choose.

### If Quick 3D is adopted

Quick 3D is **not linked today** — it is absent from `HYPERWAVE_QT_COMPONENTS`
in `CMakeLists.txt`. It is listed here because it is anticipated, and because
adopting it is the single change that would alter what may be shipped.

Note the missing option in its SPDX line: **there is no LGPLv3 for Quick 3D**.
The moment it is linked, a binary built against open-source Qt must be
distributed under **GPLv3**. Publishing this repository satisfies the
corresponding-source obligation, but the change is real and it is a one-line
change to make by accident. Under a commercial Qt licence nothing changes.

Adopting it also pulls in `qsb`, the build-time shader compiler, if
`qt_add_shaders` is used. `qsb` is a **tool**, not a library, and it is covered
by The Qt Company GPL Exception 1.0, which extends to "the output of this
application" — so compiling shaders with it does not place the compiled shaders
or the app under GPLv3. The Quick 3D runtime library is what does that.

The distinction throughout is about the build, not the repository. Someone who
clones this and builds it against open-source Qt with Quick 3D enabled produces
a GPLv3-encumbered binary even though this source is MIT.

## Code in the shaders
Two pieces of the strands visualiser are other people's work, both MIT.
**Simplex noise** in `src/render/shaders/strands.vert` is Ian McEwan / Ashima
Arts' GLSL implementation ([webgl-noise](https://github.com/ashima/webgl-noise)),
MIT, used essentially verbatim. Three octaves of it are what push the tube
around.
**The technique itself** is Matt DesLauriers' codevember #21
([mattdesl/codevember](https://github.com/mattdesl/codevember), MIT -- confirmed
against GitHub's licence API, since the LICENSE.md link in that README 404s):
strands wrapped around an axis, radius driven by the spectrum sampled along
their length, tapered at both ends, expanded to ribbons in screen space. The
shader here is a **port**, not an interpretation -- the angle, radius,
turbulence and pinch are the same maths -- so it is credited as such rather than
described as ours. What differs is the plumbing: instanced rather than a draw
call per strand, and rendered through QRhi into a QImage.
hyperwave vendors nothing. There is no `third_party/`, no submodule, no
fetched dependency, no bundled font, and no committed artwork beyond the
screenshots in `docs/`, which are our own.

The macOS backend calls Apple's own frameworks — CoreAudio, AppKit,
QuartzCore — through the public SDK. Those are part of the operating system,
are not redistributed here or in a build, and impose no terms on this source.

The application icons and the audio it visualises both belong to whoever
produced them. Neither is written to disk, and neither is redistributed: the
icons are read at runtime from the running system through `NSRunningApplication`
and LaunchServices, and the audio is metered and discarded.
