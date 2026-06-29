# MPlug Plugin Template

A starting point for building an audio plugin with the
[MPlug](https://github.com/olilarkin/mplug) framework. It builds every format
from a single plugin definition, ships a cross-platform GitHub Actions CI, and
demonstrates the fundamentals (parameters, a WebView editor, latency reporting,
and state/preset serialization).

## Getting started

1. Click **Use this template** to create your own repository.
2. Clone it, then rename the project:
   ```bash
   python rename_plugin.py AcmeFilter "Acme Audio"
   ```
   This renames the C++ class, source files, plugin id (`com.acmeaudio.acmefilter`),
   AudioUnit subtype, and CMake targets. See `python rename_plugin.py -h` for
   options. Delete `rename_plugin.py` once you've run it.
3. Add a CI secret (see [Continuous integration](#continuous-integration)).
4. Build (see below).

## Building

```bash
# Configure + build (Release, Ninja)
cmake --preset release
cmake --build --preset release

# Windows (Visual Studio 2022)
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

Plain CMake without presets also works:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

MPlug is pulled automatically via CMake `FetchContent`. To build against a local
checkout of MPlug instead of cloning it:

```bash
cmake --preset release -DFETCHCONTENT_SOURCE_DIR_MPLUG=/path/to/mplug
```

### AUv3 (macOS / iOS / visionOS)

The AudioUnit v3 app extension must be built with Xcode, so it uses CMake's
Xcode generator. There's no checked-in `.xcodeproj` — CMake generates one. Open
it in Xcode to develop, sign, and run the AUv3 on a device or simulator:

```bash
cmake --preset xcode      # macOS  -> build/xcode/MyPlugin.xcodeproj (AUv3 + desktop)
cmake --preset ios        # iOS    -> build/ios/MyPlugin.xcodeproj
cmake --preset visionos   # visionOS -> build/visionos/MyPlugin.xcodeproj
open build/ios/MyPlugin.xcodeproj
```

The generated project has `…AUv3Framework`, `…AUv3Extension`, and `…AUv3App`
(host) targets. Select the host-app scheme, set your Team in Signing &
Capabilities, and Run. On iOS/visionOS only the AUv3 is built (the desktop
formats don't apply there). CI builds these unsigned on every push.

### Requirements

- CMake 3.21+, a C++20 compiler, Git
- **macOS**: Xcode / Command Line Tools
- **Windows**: Visual Studio 2022 (WebView2 ships with Windows)
- **Linux**: `ninja-build libasound2-dev libgtk-3-dev libwebkit2gtk-4.1-dev`

## What gets built

`mplug_add_plugin()` produces, from one definition:

- **MyPluginVST3** — VST3 (cross-platform)
- **MyPluginClap** — CLAP (cross-platform)
- **MyPluginAUv2** — AudioUnit v2 (macOS)
- **MyPluginApp** — standalone application (cross-platform, WebView UI)
- **MyPluginCli** — headless command-line tool
- **MyPluginPython** — Python module

## The plugin

`src/my_plugin.h` is a simple stereo gain plugin that demonstrates the patterns
you'll actually need:

- **Parameters** — a single automatable Gain parameter (auto-serialized into state).
- **Editor** — a [CHOC](https://github.com/Tracktion/choc) WebView GUI. The
  HTML/CSS/JS lives in `resources/web/editor.html` (a single cross-platform
  source of truth) and is embedded into the binary at build time.
- **Latency** — `latency()` reports the plugin's latency in samples. A gain has
  none, so it returns 0; it's wired up as plumbing so you can return a real
  count when you add lookahead/FFT.
- **State** — `saveCustomState()/loadCustomState()` persist extra, non-parameter
  state alongside the auto-serialized parameters.

### GUI / editor status

The in-host WebView editor (embedding into a plugin host's window) is implemented
for **macOS** (WKWebView, `src/my_plugin_editor_mac.mm`) and **Windows**
(WebView2, `src/my_plugin_editor_win.cpp`) — both share the same UI and the same
bidirectional host↔UI parameter sync. On Linux, `createEditor()` returns
`nullptr` (the host shows its generic parameter UI) because WebKitGTK needs the
host's run loop bridged into the GLib main context first; that's the open work in
the `src/my_plugin_editor_generic.cpp` stub. The **standalone App provides the
full WebView UI on all three platforms** regardless.

## Customizing

1. Implement your DSP and parameters in `src/my_plugin.h`.
2. Update metadata in `CMakeLists.txt` (`PLUGIN_ID`, `AU_SUBTYPE`, `AU_TYPE`,
   `CLAP_FEATURES`, `VST3_CATEGORY`, …) — or run `rename_plugin.py` first.
3. Edit the editor HTML/CSS/JS in `resources/web/editor.html`.

## Continuous integration

`.github/workflows/build.yml` builds all formats on **macOS, Windows, and Linux**
on every push / pull request.

Because MPlug is a private repository, CI needs a token to fetch it. Add a
repository **secret** named `MPLUG_CI_TOKEN` — a fine-grained Personal Access
Token (or GitHub App token) with read access to the MPlug repo. The workflow
rewrites git URLs to use it when cloning dependencies. (If MPlug is public for
you, the secret can be empty and CI will fetch it without auth.)

The build also runs validation: **pluginval** (VST3/AU), **auval** (AU),
**clap-validator** (CLAP), and a CLI smoke test.

## Releases (signed + notarized macOS)

`.github/workflows/release.yml` runs on a version tag (`git tag v1.0.0 && git
push --tags`). It builds the macOS plugins + app, **Developer ID** codesigns
them with a hardened runtime, **notarizes** and **staples** a DMG, and attaches
it to the GitHub Release.

It needs these repository secrets (in addition to `MPLUG_CI_TOKEN`):

| Secret | What it is |
|---|---|
| `DEVELOPER_ID_APPLICATION` | `Developer ID Application: NAME (TEAMID)` |
| `BUILD_CERTIFICATE_BASE64` | base64 of your Developer ID Application `.p12` |
| `P12_PASSWORD` | password for that `.p12` |
| `KEYCHAIN_PASSWORD` | any string (temporary CI keychain password) |
| `NOTARY_KEY_BASE64` | base64 of an App Store Connect API key (`.p8`) |
| `NOTARY_KEY_ID` | the API key's Key ID |
| `NOTARY_ISSUER_ID` | the API key's Issuer ID |

Export the `.p12` from Keychain Access (`base64 -i cert.p12 | pbcopy`); create
the API key at App Store Connect → Users and Access → Integrations (role
Developer; `base64 -i AuthKey_XXXX.p8 | pbcopy`). The workflow header documents
the Apple-ID-password alternative to the API key. The app's hardened-runtime
entitlements live in `resources/entitlements.mac.plist`.

Without a tag, the workflow can be run manually (`workflow_dispatch`) to sign +
notarize and upload the DMG as a build artifact, without creating a release.
