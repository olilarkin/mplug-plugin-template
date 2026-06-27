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
- **Editor** — a [CHOC](https://github.com/Tracktion/choc) WebView GUI
  (HTML/CSS/JS) in `src/my_plugin_editor_mac.mm`.
- **Latency** — `latency()` reports the plugin's latency in samples. A gain has
  none, so it returns 0; it's wired up as plumbing so you can return a real
  count when you add lookahead/FFT.
- **State** — `saveCustomState()/loadCustomState()` persist extra, non-parameter
  state alongside the auto-serialized parameters.

### GUI / editor status

The in-host WebView editor (embedding into a plugin host's window) is currently
implemented for **macOS** only. On Windows and Linux, `createEditor()` returns
`nullptr` (the host shows its generic parameter UI), while the **standalone App
provides the full WebView UI on all three platforms**. The Windows/Linux stubs
(`src/my_plugin_editor_win.cpp`, `src/my_plugin_editor_generic.cpp`) are where
native in-host editors would go.

## Customizing

1. Implement your DSP and parameters in `src/my_plugin.h`.
2. Update metadata in `CMakeLists.txt` (`PLUGIN_ID`, `AU_SUBTYPE`, `AU_TYPE`,
   `CLAP_FEATURES`, `VST3_CATEGORY`, …) — or run `rename_plugin.py` first.
3. Edit the editor HTML in `src/my_plugin_editor_mac.mm`.

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
