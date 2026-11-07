# Unreal Tournament 2004 Modernized

An unofficial, non-commercial port of Unreal Tournament 2004 (build 3369) to modern hardware and operating systems. It is a fan project, made out of appreciation for a game that is now more than twenty years old.

## What it is

- Windows (x86, x64), Linux (x86, x64, arm64), Android (arm64) and the web (WebAssembly).
- SDL3 for windows and input, OpenGL ES 3 for rendering (ANGLE on Windows, WebGL 2 on the web), OpenAL for sound, Bullet behind the Karma physics API.
- Network compatible with retail build 3369. WebRTC transport next to UDP, so browsers can join and host.
- No editor. The command line tools of `ucc` are kept.

The game content is not part of this repository. You need the files of a UT2004 install that you own.

## Build

Third party libraries come from [vcpkg](https://github.com/microsoft/vcpkg) on Windows, Android and the web (`VCPKG_ROOT` must point to it), and from the system packages on Linux. Clone with `git submodule update --init`.

| Target | Needs | Configure | Build |
|---|---|---|---|
| Windows x64 | Visual Studio 2022 | `cmake --preset win64` | `cmake --build --preset win64-release` |
| Windows x86 | Visual Studio 2022 | `cmake --preset win32` | `cmake --build --preset release` |
| Linux | GCC, Ninja, CMake 3.26, SDL 3.2 | `cmake --preset linux64` | `cmake --build --preset linux64-release` |
| Android arm64 | JDK 17, SDK platform 36, NDK r28c | `cmake --preset android-arm64` | `cmake --build --preset android-arm64-release` |
| Web | emsdk 7.0.0 | `cmake --preset web` | `cmake --build --preset web-release` |

Linux packages on Debian and Ubuntu: `build-essential ninja-build pkg-config zlib1g-dev libvorbis-dev libopenal-dev libbullet-dev libbullet-extras-dev libgles-dev libsquish-dev libssl-dev libcurl4-openssl-dev nlohmann-json3-dev libspeexdsp-dev`, plus SDL 3.2 or newer.

The output lands in `build/<preset>/System/`. The Android build writes `build/android-arm64/UT2004.apk` and the web build writes `build/web/`.

### Script packages

`cmake --build --preset <build preset> --target ucc_make` fills `build/Scripts/` with the compiled UnrealScript packages. Online play needs packages identical to retail, so set `UT_RETAIL_DIR` to your UT2004 install first: the retail packages are copied from there and only this port's two fix packages are compiled. Android and the web take the packages from `build/Scripts/`, so run a desktop `ucc_make` before building them.

## Run

Start `UT2004.exe` (`ut2004-bin` on Linux) in `build/<preset>/System/`. On the first start it asks for the folder of your UT2004 install and uses the content in place.

A dedicated server is `UCC server DM-Rankin` (`ucc-bin` on Linux). It does not ask for content, add the `Paths=` lines of your install to its `UT2004.ini`.

The Android app downloads its content on the first launch, or takes an archive or folder you point it to. The web page is served from any static host, for example `python -m http.server 8000 -d build/web`.

## Files and logs

| Platform | Settings, saves and `UT2004.log` |
|---|---|
| Windows | `build/<preset>/System/` |
| Linux | `~/.ut2004/System/` (next to the binary with `-nohomedir`) |
| Android | `Android/data/org.ut2004.game/files/UT2004/System/` |
| Web | the browser's IndexedDB for the page |

## Network connections

The game only connects when you use an online feature. These are the defaults in `System/Default.ini`, and all of them can be changed or emptied there.

| What | Where | When |
|---|---|---|
| Community master servers | `utmaster.openspy.net`, `ut2004master.333networks.com` (port 28902) | The server browser is open, or you host an advertised server |
| Web master | `master.ut2004.dev` | The same, for servers and clients that use WebRTC |
| STUN | `stun.cloudflare.com` | Joining or hosting a browser hosted game |
| Content host | `assets.ut2004.dev` | First launch on Android and on the web |
| Game servers | The server you pick | Joining a game |

Stats tracking is off until you turn it on in the settings. There is no telemetry.

## Licensing

See `NOTICE.md`. In short: Epic's code and content stay Epic's, the files this port added are under the Mozilla Public License 2.0, and third party libraries keep their own licenses (`System/THIRD_PARTY.md`).

Security reports: see `SECURITY.md`.

## Ownership

Unreal, Unreal Tournament, Unreal Tournament 2004 and the Unreal Engine are trademarks or registered trademarks of Epic Games, Inc. The original source code and all game content belong to Epic Games, Inc. or their respective owners. This project is not affiliated with, endorsed by, or sponsored by Epic Games, Inc.

This repository grants no license to the original code or content. Third-party components remain under their own licenses.

## Free of charge

This project is free of charge and is not sold or monetized in any way.

## Contact with Epic Games

I have tried for several years to reach Epic Games about this project, by email and through other channels, without receiving a reply.

If you represent Epic Games and have any request or concern about this repository, including a request to take it down, please contact me at kecskes.adam@outlook.com. I will respond promptly and comply with any reasonable request.
