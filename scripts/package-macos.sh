#!/usr/bin/env bash
#
# Sign a built hyperwave.app and wrap it in a DMG.
#
# Run by CI on a tag, and runnable by hand to test the update path end to end
# without waiting on a release. Notarization is separate (see RELEASING.md) --
# this stops at a signed, stapled-ready bundle.
#
#   scripts/package-macos.sh build 0.1.0 arm64
#
# Arguments:
#   $1  build directory containing hyperwave.app   (default: build)
#   $2  version string used in the DMG's filename  (default: from CMake)
#   $3  architecture tag for the filename          (default: uname -m)
#
# Signing identity comes from CODESIGN_IDENTITY, or the first "Developer ID
# Application" in the keychain. Unset and absent means the bundle is left ad-hoc
# signed: fine for a local look, useless for distribution, and said out loud
# rather than assumed.
set -euo pipefail

BUILD_DIR="${1:-build}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$BUILD_DIR/hyperwave.app"

VERSION="${2:-}"
if [ -z "$VERSION" ]; then
    VERSION=$(sed -n 's/^project(hyperwave VERSION \([0-9.]*\).*/\1/p' \
                  "$REPO_ROOT/CMakeLists.txt")
fi
ARCH="${3:-$(uname -m)}"
DMG="$BUILD_DIR/hyperwave-${VERSION}-macos-${ARCH}.dmg"

[ -d "$APP" ] || { echo "no bundle at $APP -- build first" >&2; exit 1; }

# --- the entitlements have to be valid XML ---------------------------------
#
# Checked here, in the first second of the script, because codesign checks it
# in the last: an invalid file is only rejected when the app itself is signed,
# which is after macdeployqt, the prune, the thin and forty individual
# signatures. On a runner that is several minutes to be told
# "AMFIUnserializeXML: syntax error near line 13".
#
# `plutil -lint` is NOT sufficient and is the reason this exists. It accepted a
# file whose comment contained a double hyphen -- illegal inside an XML comment,
# and rejected by the parser Apple's code signing actually uses. xmllint catches
# it; plutil does not.
ENTITLEMENTS="$REPO_ROOT/hyperwave-release.entitlements"
if command -v xmllint >/dev/null; then
    if ! xmllint --noout "$ENTITLEMENTS" 2>/tmp/hw-ent-err; then
        echo "ERROR: $ENTITLEMENTS is not valid XML:" >&2
        sed 's/^/       /' /tmp/hw-ent-err >&2
        rm -f /tmp/hw-ent-err
        exit 1
    fi
    rm -f /tmp/hw-ent-err
fi

# --- and they have to let the built-in mic through -------------------------
#
# The hardened runtime silently refuses the built-in microphone to an app
# without com.apple.security.device.audio-input: the IO proc runs and delivers
# zeros, with no prompt and no error. USB mics are not gated the same way, and
# local builds are not hardened, so nothing short of a released build on a
# MacBook shows it. v0.2.0 shipped that way.
if ! grep -q "<key>com.apple.security.device.audio-input</key>" "$ENTITLEMENTS"; then
    echo "ERROR: $ENTITLEMENTS is missing com.apple.security.device.audio-input." >&2
    echo "       A release signed without it never hears the built-in mic." >&2
    exit 1
fi

# --- is the bundle actually current? ---------------------------------------
#
# This script signs and packages; it does NOT compile. Refuse to package a
# bundle older than its own inputs, because nothing downstream catches it -- the
# signature is over whatever is there, so a stale asset produces a perfectly
# valid DMG of the wrong thing. CI always builds immediately before this, so it
# only ever fires locally, which is exactly where the mistake is possible.
#
# Compared against the newest file anywhere in the bundle rather than against
# the executable: changing a resource copies a new file in without relinking, so
# anchoring on the binary would report every resource edit as stale forever.
#
# `awk NR==1` rather than `head -1`: head exits after the first line, the writer
# upstream takes SIGPIPE, and under `set -o pipefail` that is a 141 that `set -e`
# turns into a silent abort of the whole script. awk drains the stream.
newest() {
    find "$@" -type f -exec stat -f '%m %N' {} + 2>/dev/null | sort -rn | awk 'NR==1'
}
APP_NEWEST=$(newest "$APP")
SRC_NEWEST=$(newest "$REPO_ROOT/src" "$REPO_ROOT/qml")
if [ "${SRC_NEWEST%% *}" -gt "${APP_NEWEST%% *}" ] 2>/dev/null; then
    echo "ERROR: $APP is older than ${SRC_NEWEST#* }" >&2
    echo "       This script packages; it does not build." >&2
    echo "       Run:  cmake --build $BUILD_DIR" >&2
    exit 1
fi

# --- clear stale mounts ----------------------------------------------------
#
# create-dmg drives Finder with AppleScript that addresses the volume by NAME.
# With /Volumes/hyperwave already taken the new image mounts as "hyperwave 1",
# the script talks to the wrong volume, and the run hangs until something kills
# it. A killed run then leaves its own scratch image attached.
for vol in /Volumes/hyperwave /Volumes/hyperwave\ *; do
    [ -d "$vol" ] || continue
    echo "detaching stale volume: $vol"
    hdiutil detach "$vol" -force -quiet 2>/dev/null || true
done
# Orphaned create-dmg scratch mounts -- but only ones backed by an image in THIS
# build directory. Never somebody else's disk image.
hdiutil info | awk '
    /^image-path/  { p = $0; sub(/^image-path[ \t]*:[ \t]*/, "", p) }
    /^\/dev\/disk/ { if ($NF ~ "^/Volumes/" && p ~ "/rw\\..*hyperwave.*\\.dmg$") print $NF }
' | while IFS= read -r m; do
    echo "detaching orphaned scratch mount: $m"
    hdiutil detach "$m" -force -quiet 2>/dev/null || true
done

# --- Qt --------------------------------------------------------------------
#
# macdeployqt copies the Qt frameworks and QML modules in. It runs before
# signing, because it rewrites the very binaries that get signed.
#
# Which macdeployqt matters more than it looks: a Qt5 one on PATH fails in a way
# that looks like success -- it follows the binary's real dependencies so the
# Qt6 frameworks land correctly, then deploys Qt5 plugins and QML modules and
# never the Qt6 ones. The result runs on the build machine and dies everywhere
# else. So the build's own CMakeCache wins: that is the Qt the binary was linked
# against, and it cannot disagree with itself.
MACDEPLOYQT=""
if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    QT_PREFIX=$(sed -n 's|^Qt6_DIR:PATH=\(.*\)/lib/cmake/Qt6$|\1|p' \
                    "$BUILD_DIR/CMakeCache.txt" | awk 'NR==1')
    [ -n "$QT_PREFIX" ] && [ -x "$QT_PREFIX/bin/macdeployqt" ] \
        && MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
fi
if [ -z "$MACDEPLOYQT" ] && [ -n "${QT_ROOT_DIR:-}" ] \
   && [ -x "$QT_ROOT_DIR/bin/macdeployqt" ]; then
    MACDEPLOYQT="$QT_ROOT_DIR/bin/macdeployqt"
fi
[ -n "$MACDEPLOYQT" ] || MACDEPLOYQT="$(command -v macdeployqt || true)"
[ -n "$MACDEPLOYQT" ] || { echo "macdeployqt not found" >&2; exit 1; }

if otool -L "$MACDEPLOYQT" 2>/dev/null | grep -q "libQt5"; then
    echo "ERROR: $MACDEPLOYQT is a Qt5 tool; this is a Qt6 app." >&2
    echo "       It would deploy Qt5 QML modules and omit the Qt6 ones," >&2
    echo "       producing a bundle that only runs on this machine." >&2
    exit 1
fi
echo "macdeployqt: $MACDEPLOYQT"
"$MACDEPLOYQT" "$APP" -qmldir="$REPO_ROOT/qml" -verbose=1

# --- embed Sparkle ---------------------------------------------------------
#
# macdeployqt only knows about Qt. Sparkle is linked from wherever FetchContent
# unpacked it, which is inside the build tree and will not exist on a user's
# machine, so the framework has to be copied in and the install name rewritten
# to the copy. Skipped silently when the build has no updater -- that is the
# normal state of a build made without a signing key.
# `|| true` is load-bearing under `set -euo pipefail`: with no updater in
# this build there is no _deps at all, find exits 1, and the pipeline
# failure aborts the whole script -- silently, right after macdeployqt.
SPARKLE_SRC=""
if [ -d "$BUILD_DIR/_deps" ]; then
    SPARKLE_SRC=$(find "$BUILD_DIR/_deps" -maxdepth 4 -type d -name "Sparkle.framework" \
                       2>/dev/null | awk 'NR==1' || true)
fi
if [ -n "$SPARKLE_SRC" ]; then
    echo "embedding Sparkle from $SPARKLE_SRC"
    mkdir -p "$APP/Contents/Frameworks"
    rm -rf "$APP/Contents/Frameworks/Sparkle.framework"
    cp -R "$SPARKLE_SRC" "$APP/Contents/Frameworks/"
    install_name_tool -change \
        "@rpath/Sparkle.framework/Versions/B/Sparkle" \
        "@rpath/Sparkle.framework/Versions/B/Sparkle" \
        "$APP/Contents/MacOS/hyperwave" 2>/dev/null || true
else
    echo "no Sparkle in the build tree -- packaging without an updater"
fi

# --- prune what macdeployqt guessed at -------------------------------------
#
# Its QML import scan is speculative: it walks the modules our imports could
# reach rather than the ones they do, and bundles the union. hyperwave's only
# QML is the settings window. Pruned here rather than relying on CI happening to
# install a smaller Qt, so local and CI agree and the intent is reviewable.
#
# Must run BEFORE signing -- anything added or removed afterwards breaks the
# seal.
prune() {
    local before after
    before=$(du -sm "$APP" | cut -f1)
    rm -rf "$@"
    after=$(du -sm "$APP" | cut -f1)
    echo "  pruned $((before - after)) MB"
}

echo "pruning Qt3D and Scene3D:"
prune "$APP/Contents/Resources/qml/QtQuick/Scene3D" \
      "$APP"/Contents/Frameworks/Qt3D*.framework \
      "$APP/Contents/PlugIns/sceneparsers" \
      "$APP/Contents/PlugIns/geometryloaders" \
      "$APP/Contents/PlugIns/renderers"

# hyperwave reads audio through CoreAudio directly. QtMultimedia is not linked
# and the FFmpeg backend behind it is ~28 MB of libavcodec alone.
echo "pruning multimedia:"
prune "$APP/Contents/PlugIns/multimedia" \
      "$APP"/Contents/Frameworks/QtMultimedia*.framework \
      "$APP"/Contents/Frameworks/libav*.dylib \
      "$APP"/Contents/Frameworks/libsw*.dylib \
      "$APP/Contents/Resources/qml/QtMultimedia"

echo "pruning unused modules:"
prune "$APP/Contents/PlugIns/sqldrivers" \
      "$APP"/Contents/Frameworks/QtSql.framework \
      "$APP"/Contents/Frameworks/QtQuick3D*.framework \
      "$APP"/Contents/Frameworks/QtQuickDialogs2*.framework \
      "$APP/Contents/Resources/qml/QtQuick/Dialogs" \
      "$APP/Contents/Resources/qml/QtQuick3D"

# --- thin the universal slices ---------------------------------------------
#
# Qt's macOS installer ships universal frameworks, so an arm64-only build still
# carries a complete x86_64 copy of Qt that can never execute -- close to half
# the bundle. Skipped when our own binary is universal; then both slices are
# real. Must run BEFORE signing, since rewriting a Mach-O invalidates its seal.
THIN_ARCH=$(lipo -archs "$APP/Contents/MacOS/hyperwave" 2>/dev/null || echo "")
if [ "$(printf '%s' "$THIN_ARCH" | wc -w)" -eq 1 ]; then
    before=$(du -sm "$APP" | cut -f1)
    thinned=0
    while IFS= read -r -d '' f; do
        archs=$(lipo -archs "$f" 2>/dev/null) || continue
        case "$archs" in *' '*) ;; *) continue ;; esac      # already single-arch
        case " $archs " in *" $THIN_ARCH "*) ;; *) continue ;; esac
        if lipo -thin "$THIN_ARCH" "$f" -output "$f.thin" 2>/dev/null; then
            # Written back through the original file rather than moved over it,
            # so permissions and the inode survive.
            cat "$f.thin" > "$f" && thinned=$((thinned + 1))
            rm -f "$f.thin"
        fi
    done < <(find "$APP" -type f -print0)
    after=$(du -sm "$APP" | cut -f1)
    echo "thinned $thinned binaries to $THIN_ARCH: $((before - after)) MB"
else
    echo "universal build ($THIN_ARCH) -- not thinning"
fi

# --- signing ---------------------------------------------------------------
#
# The identity matters more here than in a normal app: TCC keys the
# system-audio grant to the signature, so signing with a different identity
# than the last build makes macOS treat this as a brand new app and the user
# has to grant permission again. See the codesign block in CMakeLists.txt.
IDENTITY="${CODESIGN_IDENTITY:-}"
if [ -z "$IDENTITY" ]; then
    IDENTITY=$(security find-identity -v -p codesigning 2>/dev/null \
               | grep "Developer ID Application" | awk 'NR==1' \
               | grep -o '"[^"]*"' | tr -d '"' || true)
fi

if [ -n "$IDENTITY" ]; then
    echo "Signing with: $IDENTITY"
    # Inside out. codesign seals what it finds, so anything signed after its
    # container invalidates that container's seal -- and the failure shows up at
    # launch on someone else's machine, not here.
    find "$APP" \( -name "*.dylib" -o -name "*.so" \) -print0 \
        | xargs -0 -r -I{} codesign --force --options runtime --timestamp \
                                    --sign "$IDENTITY" {}
    # Frameworks are signed through Versions/<X>, never through the framework
    # directory itself: a versioned framework's top level is a set of symlinks
    # into the current version, and codesign cannot tell that shape apart from
    # an app bundle -- it stops with "bundle format is ambiguous".
    for fw in "$APP"/Contents/Frameworks/*.framework; do
        [ -d "$fw" ] || continue
        # Sparkle carries its own executables: two XPC services, the Autoupdate
        # helper and the progress app. Each is a bundle in its own right and has
        # to be sealed before the thing containing it. -maxdepth keeps this off
        # the symlinked duplicates at the framework root, which would otherwise
        # be signed twice and break the seal made the first time.
        for ver in "$fw"/Versions/*; do
            [ -d "$ver" ] && [ ! -L "$ver" ] || continue
            find "$ver" -maxdepth 2 \
                 \( -name "*.xpc" -o -name "*.app" -o -name "Autoupdate" \) -print0 \
                | xargs -0 -r -I{} codesign --force --options runtime \
                                            --timestamp --sign "$IDENTITY" {}
            codesign --force --options runtime --timestamp --sign "$IDENTITY" "$ver"
        done
    done
    # The RELEASE entitlements, not the development ones. The development pair
    # carries get-task-allow so a debugger can attach, and Apple refuses to
    # notarize anything signed with it.
    codesign --force --options runtime --timestamp \
             --entitlements "$ENTITLEMENTS" \
             --sign "$IDENTITY" "$APP"

    # Refuse to hand Apple something it is guaranteed to reject.
    #
    # Cheap, local, and immediate, where the same mistake costs a full build,
    # two signings and a round trip to the notary service to learn about
    # otherwise -- and arrives as "Invalid" with no reason attached.
    if codesign -d --entitlements - "$APP" 2>/dev/null | grep -q "get-task-allow"; then
        echo "ERROR: $APP is signed with get-task-allow." >&2
        echo "       Notarization will fail. Check which entitlements file" >&2
        echo "       was used -- it must be hyperwave-release.entitlements." >&2
        exit 1
    fi
    if ! codesign -d --entitlements - "$APP" 2>/dev/null | grep -q "device.audio-input"; then
        echo "ERROR: $APP is signed without com.apple.security.device.audio-input." >&2
        echo "       The built-in mic would deliver silence. Check the entitlements." >&2
        exit 1
    fi
    codesign --verify --deep --strict --verbose=2 "$APP"
else
    echo "WARNING: no Developer ID identity found. The bundle stays" >&2
    echo "         ad-hoc signed -- Gatekeeper will refuse it elsewhere," >&2
    echo "         and the system-audio grant will not survive a rebuild." >&2
fi

# --- DMG -------------------------------------------------------------------
command -v create-dmg >/dev/null || brew install create-dmg

STAGE="$BUILD_DIR/dmg_staging"
rm -rf "$STAGE" "$DMG"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"

# create-dmg returns 2 when it produced the image but could not apply the window
# layout, which happens on a headless runner with no Finder. The image is still
# good, so the file's existence is the real test.
# There is no background art yet, so this stays optional. Expanded with the
BACKGROUND=()
if [ -f "$REPO_ROOT/packaging/macos/dmg-background.tiff" ]; then
    BACKGROUND=(--background "$REPO_ROOT/packaging/macos/dmg-background.tiff")
fi
create-dmg \
    --volname "hyperwave" \
    --format ULMO \
    --window-pos 200 120 \
    --window-size 660 400 \
    --icon-size 110 \
    --icon "hyperwave.app" 165 185 \
    --hide-extension "hyperwave.app" \
    --app-drop-link 495 185 \
    ${BACKGROUND[@]+"${BACKGROUND[@]}"} \
    --no-internet-enable \
    "$DMG" "$STAGE" || true

rm -rf "$STAGE"
[ -f "$DMG" ] || { echo "DMG was not created" >&2; exit 1; }
echo "$DMG"
