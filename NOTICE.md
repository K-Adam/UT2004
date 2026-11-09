# Licensing

This tree holds code from three sources, each under its own terms.

## Epic Games

The engine, the game code, the UnrealScript packages and all game data belong to Epic Games, Inc. They are not covered by the `LICENSE` file and this repository grants no license to them. See the Ownership section of `README.md`.

## This port

The files this port added, and the changes it made to existing files as far as their author can license them, are under the Mozilla Public License 2.0 in `LICENSE`. That covers:

- `Android/` (apart from `app/src/main/java/org/libsdl/app`, which is SDL's)
- `Emscripten/`, `DevIni/`, `ports/`, `triplets/`
- `GUI2K4Fix/`, `XAdminFix/`
- `SDLDrv/`, `SDLLaunch/`, `OpenGLDrv/` (apart from the vendored files listed in `System/THIRD_PARTY.md`)
- `IpDrv/Src/WebMaster.cpp`, `IpDrv/Src/WebRtc.cpp`, `IpDrv/Src/WebRtc.h`, `IpDrv/Src/WebRtcCommon.cpp`, `IpDrv/Src/WebRtcBrowser.cpp`
- `Engine/Src/UnNicknameWords.h`
- every `CMakeLists.txt`, `CMakePresets.json` and `vcpkg.json`

The files carry no license header. This notice takes its place, as Exhibit A of the license allows.

In short: you may use, change and ship these files, in open or closed projects. If you distribute changed versions of them, those files stay under the MPL, and you say where they came from: https://github.com/K-Adam/UT2004

## Third parties

Bundled and linked libraries keep their own licenses. `System/THIRD_PARTY.md` lists them with their license texts.
