# 007: From Russia with Love — Android

*[Español](README.md)*

An unofficial native Android port of the original Xbox version of *007: From Russia with Love*, with on-screen touch controls, controller support and a 60 FPS target.

The game's x86 code is statically recompiled ahead of time to C++ with [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) and built for arm64. Drawing goes through Vulkan, and windowing, input and audio through SDL3. No emulator is involved.

The port is built on the PC port [007FRWL-XBERecomp](https://github.com/TGP482/007FRWL-XBERecomp) by TGP482, and on the Android host of [Black for Android](https://github.com/RanggaCasper/Black-Android) by Rangga Casper. See [Credits](#credits).

> [!NOTE]
> This project does not include any game assets. You must provide the files from your own legally acquired copy of the game to install or build.

## Download

The APK is on the [Releases](https://github.com/M1LT0N3R2431/007-Project-Russia/releases) page. It is signed with the debug key, so it installs as it is.

## Requirements

- **Phone:** arm64 device with Vulkan 1.1, Android 8.0 (API 26) or newer.
- **Game:** the USA or Europe release of the Xbox version, either as `default.xbe` with its data folders, or as a disc image (`.iso`).

## Installation

1. Install the APK.
2. Open **007 FRWL** and allow **all files access** when it asks (on Android 10, the **storage** permission).
3. Copy the game into `/storage/emulated/0/007FRWL`, or into a `007FRWL` folder at the top of your SD card. The app creates the folder once access is allowed and starts the game as soon as it finds the files.
4. If you don't grant that access, `Android/data/com.frwl.recomp/files/assets` works too.

Settings (`settings.toml`, `layout.toml`), saves (`hdd`), logs and the shader cache are stored in `Android/data/com.frwl.recomp/files`.

## Touch controls

The controls are the game's own pad, laid over the screen, and they change with what Bond is doing.

| Control | Action |
| --- | --- |
| Left stick | Move |
| Drag on the right side | Look |
| Fire (R) | Fire, hold to aim |
| Lock on (L) | Lock on / free aim |
| Action (A) | Jump, climb, use, interact |
| Focus (X) | Focus / first person look |
| Wall cover (Y) | Take and leave cover |
| Dive roll (B) | Dive roll |
| Reload | Reload |
| < / > | Previous / next weapon |
| Crouch (L3) | Crouch |
| Inventory (BACK) | Item select / inventory |
| Pause (START) | Pause menu |
| Back (BACK, menus) | Back out of a menu |
| Gear | Settings panel |

Driving and the jetpack get their own buttons in the console's places: accelerate on A, brake on X, handbrake and land on B, slasher and exit on Y.

The layout can be moved and resized: open the settings panel (gear), then **Touch Controls → Edit layout**. Drag a control to move it, pinch it to resize it, and use the bar at the top to reset, hide, cancel or save. The layout is kept in `layout.toml`.

## Building

### Prerequisites

- Windows, with Visual Studio 2022 and the **Desktop development with C++** workload (the recompiler links with its tools).
- Android SDK Platform 34, NDK `28.2.13676358` and CMake `3.31.6`, installed through Android Studio's SDK Manager.
- JDK 17 or newer (`JAVA_HOME`).
- Git, and Python 3 if you build from an `.iso`.
- Your own copy of the game, USA or Europe release.

### Steps

1. Put `default.xbe` and its data folders in `assets\`, or put the game's `.iso` in `assets\` or `iso\`. The build takes the `default.xbe` out of the image with `tools\xdvdfs.py`.
2. Run `build.bat`.
3. Install `app\build\outputs\apk\release\app-release.apk`.

The first build recompiles the game's code and takes 30 to 90 minutes; later builds only compile what changed. For development, `gradlew.bat assembleDebug` builds a debug APK once `generated\game` exists.

### Project layout

- `native/` — the Android host: Vulkan renderer, touch controls, input, audio and storage.
- `src/` — the game's hooks and the settings, prompts and menus ported from the PC port.
- `config/game.toml` — the recompiler's input: symbols, extra entry points and the host's hooks.
- `app/` — the Android app: activity, manifest, icon and Gradle build.
- `generated/` — the recompiled game code, written by the build and not part of the repository.

## Credits

This port would not exist without these projects:

- [007FRWL-XBERecomp](https://github.com/TGP482/007FRWL-XBERecomp) by **TGP482** — the PC port: the game's hooks, the settings and menus, and the recompilation config this port starts from.
- [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by **TGP482** — the static recompiler and the portable runtime (Xbox kernel, XDK libraries and Direct3D 8) the game runs on.
- [Black for Android](https://github.com/RanggaCasper/Black-Android) by **Rangga Casper** — the Android host this port is built on: the Vulkan renderer, the touch controls and their editor, the input and storage layers, and the Gradle/CMake build.
- [SCDA-XBERecomp-android](https://github.com/mogeldev/SCDA-XBERecomp-android) by **mogeldev** — a reference for bringing an XboxXBE-Recomp port to Android.
- [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus) and [Dear ImGui](https://github.com/ocornut/imgui) — windowing, input and audio; shader compilation; settings files; and the settings panel.
- [xemu](https://github.com/xemu-project/xemu) and [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded), which XboxXBE-Recomp used as references for Xbox hardware and API behavior.

Third-party licenses are listed in [THIRD_PARTY.txt](THIRD_PARTY.txt).

## Legal

*007: From Russia with Love* is a trademark of Danjaq, LLC and Metro-Goldwyn-Mayer. This project is not affiliated with or endorsed by them, Electronic Arts, EON Productions or any of their subsidiaries.

No game assets are included or distributed. You must own the game to use this project. The launcher icon uses artwork from the game and is not covered by the MIT license.

## License

The source code is released under the MIT license, see [LICENSE](LICENSE).
