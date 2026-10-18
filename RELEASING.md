# Releasing hyperwave

The pipeline is `.github/workflows/release.yml`. A `v*` tag builds every
platform, signs, notarizes, packages, signs again for the appcast, and
publishes to R2. A pull request builds and tests and stops there.

This has now shipped a release. v0.1.0 went out on 2026-08-29, on the third
attempt: the first two tag builds failed in notarization over the entitlements
file. Signing, notarization, both appcasts and the R2 upload have all run
against the real secrets. Read [What is not done](#what-is-not-done) anyway --
the Windows half of the pipeline is still less proven than the macOS half.

## Before the first release

### 1. The update signing key

Sparkle and WinSparkle verify the same ed25519 signature, so there is one pair
for both platforms. hyperwave uses its own, chosen deliberately rather than
whichever key the keychain happens to hand back -- see below for why those are
not the same thing.

Two halves, two homes:

- The **public** half goes in `CMakeLists.txt` as the default for
  `HYPERWAVE_EDDSA_PUBKEY`. It belongs in the tree rather than in a secret: it
  ships inside every binary anyway, so hiding it buys nothing, and as a secret
  it becomes an invisible input that can silently disagree with the private
  half.
- The **private** half goes in the `SPARKLE_EDDSA_PRIVATE_KEY` repository
  secret, and nowhere else.

Until the public key is set, `HYPERWAVE_AUTO_UPDATE` turns itself off and the
app builds with no updater and no "Check for Updates..." menu entry. That is
deliberate: an updater that cannot verify a signature is worse than no updater.

The two halves disagreeing is the one failure here that looks like success --
`generate_appcast` warns, omits the signature, and exits 0, producing a feed no
client will ever install from. The publish job counts signed enclosures and
fails the release rather than shipping that.

#### Give the key its own keychain account

By default `generate_keys` stores the private key in the login keychain under
the account `ed25519`, which is one slot shared by every Sparkle application you
build on that machine. `--account` scopes it instead, and Sparkle's own help
recommends a separate account per organization:

```
generate_keys --account hyperwave
```

That generates a pair in its own slot and leaves any existing default key
untouched. To read the halves back afterwards:

```
generate_keys --account hyperwave -p                       # public half, for CMakeLists
generate_keys --account hyperwave -x hyperwave-private.pem # private half, for the secret
```

Paste the exported file into `SPARKLE_EDDSA_PRIVATE_KEY` and then delete it --
its contents are the key itself, not a wrapper around it.

**The trap is the default, not a destructive flag.** Run plain `generate_keys`
with a key already in the default slot and it does not generate anything: it
reuses the key that is there and prints its public half. Do that on a machine
that already ships another Sparkle application and you will quietly adopt that
application's signing key, having believed you made a new one. Nothing warns
you, and the two products are then bound together for as long as both ship.

`-f` is not a force flag. It takes a file and *imports* a private key into the
keychain -- what you use to move a key between machines after `-x`. Sparkle's
help notes that an existing key item may have to be removed by hand first, so it
does not silently replace one either.

Whichever account it lives in, the release itself never reads the keychain: the
workflow signs with `--ed-key-file` from the secret. That is deliberate.
`generate_appcast` would otherwise use the keychain's default account, which on
a multi-application machine may be a different key entirely -- and a feed signed
with the wrong key looks completely successful until a user's copy refuses the
update.

If you need the tooling, take it from the Sparkle release:

```
curl -sL https://github.com/sparkle-project/Sparkle/releases/download/2.9.0/Sparkle-2.9.0.tar.xz \
  | tar xJ ./bin/generate_appcast ./bin/sign_update ./bin/generate_keys
```

### 2. Repository secrets

| Secret | What it is |
|---|---|
| `SPARKLE_EDDSA_PRIVATE_KEY` | Private half of the key above. Signs both feeds |
| `APPLE_DEVELOPER_ID_CERT_BASE64` | Developer ID Application cert as a base64 `.p12` |
| `APPLE_DEVELOPER_ID_CERT_PASSWORD` | Password for that `.p12` |
| `APPLE_NOTARIZATION_APPLE_ID` | Apple ID used for notarization |
| `APPLE_NOTARIZATION_PASSWORD` | An **app-specific** password, not the account one |
| `APPLE_NOTARIZATION_TEAM_ID` | Your ten-character Apple Developer team id |
| `CLOUDFLARE_R2_ACCESS_KEY_ID` | R2 token with write access to `hyperwave-builds` |
| `CLOUDFLARE_R2_SECRET_ACCESS_KEY` | Its secret half |
| `CLOUDFLARE_R2_ENDPOINT` | `https://<account-id>.r2.cloudflarestorage.com` |
| `AZURE_TENANT_ID` | Trusted Signing, for the Windows installer |
| `AZURE_APPLICATION_ID` | " |
| `AZURE_CLIENT_SECRET` | " |
| `AZURE_SIGNING_ACCOUNT` | " |
| `AZURE_CERT_PROFILE` | " |

The five Azure ones are only needed to sign the Windows installer. Without them
that step fails on a tag; everything else still completes, and an unsigned
installer works behind a SmartScreen warning.

### 3. The bucket and its domain

The workflow writes to `hyperwave-builds` and the feeds it generates point at
`https://hyperwave-builds.hypernuclear.com`. That custom domain has to exist and
be attached to the bucket before the first release, because the URL is baked
into the appcast and into every shipped binary's `SUFeedURL`.

Layout it produces:

```
releases/<version>/hyperwave-<version>-macos-arm64.dmg   direct download
releases/<version>/hyperwave-<version>-macos-arm64.zip   what Sparkle installs
releases/<version>/hyperwave-<version>-macos-x64.dmg
releases/<version>/hyperwave-<version>-macos-x64.zip
releases/<version>/hyperwave-<version>-windows-x64.exe   installer and update
latest/mac/appcast.xml
latest/win/appcast.xml
```

## Cutting a release

1. Bump `project(hyperwave VERSION x.y.z)` in `CMakeLists.txt` on a
   `release/vx.y.z` branch, with a CHANGELOG entry. It gets `ci`'s build like
   any other pull request; the tag build right after it is the real one.
2. Merge it. `tag-on-merge.yml` tags the merge commit `vx.y.z`, which starts
   the release. It checks that the branch name, the CMake version and the
   CHANGELOG agree first, and needs the `RELEASE_TAG_PAT` secret (a token with
   contents: write on this repo; `GITHUB_TOKEN` cannot start another
   workflow). If it fails, tag by hand: `git tag vx.y.z && git push origin vx.y.z`.
3. Watch the run. The publish job uploads artifacts first and the feeds last: a
   feed naming a file nobody has uploaded sends every running copy of the app to
   a 404, and they will already have told the user an update is waiting.

## Testing without cutting a release

The release workflow accepts `workflow_dispatch`, so the whole pipeline can be
dry-run from the Actions tab without creating a tag. Everything gated on
`refs/tags/v` -- signing, notarization, the appcast, the upload -- stays off, so
what it actually exercises is the build and the packaging. That matters because
pull requests are built by `ci`, which does not package: without a dispatch run,
`package-macos.sh` and the Inno installer are first exercised by the tag itself.

`scripts/package-macos.sh` also runs by hand and is the whole macOS path except
notarization:

```
cmake --build build
CODESIGN_IDENTITY="" ./scripts/package-macos.sh build 0.0.0-test arm64
```

It refuses to package a bundle older than its own sources, because it signs and
packages but does not compile -- and a stale asset produces a perfectly valid
DMG of the wrong thing.

## What is not done

- **No MSIX.** The Store package needs a full set of tile assets at fixed
  scales. `scripts/gen_icons.py` produces the master they would come from, so
  this is a matter of adding the sizes and an `AppxManifest.xml` rather than of
  missing artwork. The EXE is what WinSparkle downloads to apply an update, so
  it is the one that had to exist first.
- **The icon is generated, not designed.** `scripts/gen_icons.py` builds it from
  `assets/hyperwave.svg` on the signature gradient. Drop a 1024px
  `resources/app_icon_master.png` in place and re-run to replace it with real
  artwork; nothing downstream needs to change.
- **No DMG background art.** `packaging/macos/dmg-background.tiff` is optional
  in the script; without it create-dmg uses a plain window.
- **No Windows update has ever been applied.** The installer builds, packages
  and is signed on a tag, and it runs on a real machine. What has not happened
  is WinSparkle finding a newer build in the feed and installing it over an
  older one -- v0.1.0 was the first release, so there was nothing to update
  from. v0.2.0 is the first build that can be reached by an update rather than
  a download, on either platform.
- **The macOS update path is equally new.** Sparkle reads the feed and verifies
  the signature at v0.1.0, but no copy has yet installed a newer version over
  itself.
