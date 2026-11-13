<p align="center">
  <img src="dist/branding/lichii_icon.png" alt="TerraSwitch 2" width="200">
</p>

# TerraSwitch 2

TerraSwitch 2 is an experimental, open-source Nintendo Switch and Switch 2 emulator written in C++.
It is a fork of [Litchii](https://github.com/zortos293/Lichii) and Fritchii, which themselves build on
Eglantine, Eden, yuzu and Sudachi. Their contributions are kept and credited; this fork adds its own
launcher, settings and renderer work.

This is an independent fork. TerraSwitch 2 is not affiliated with Nintendo, nor an official release of
any upstream project.

## What this fork adds

- **Launcher home screen** with a game library, a cover/title panel and a rail of tool buttons.
- **Six one-click graphics presets** in the Performance menu: *Super FPS*, *Max FPS*, *Performance*,
  *Balanced*, *Quality* and *Max Quality*. Picking *Super FPS* again turns it off and restores the
  settings it replaced.
- **Frame generation up to ×8** with FSR 3. The Lossless Scaling chain holds three generated frames,
  so it stays capped at ×4.
- **FPS target and status chips** for the settings that are currently active, plus live application of
  changed settings to a running game.
- **Self-contained SVG glyph rendering** for the launcher icons, so the static Qt build does not need
  the Qt SVG plugin.

## Running it

Builds are produced from source (see below). On Windows, extract the release archive and run
**terraswitch2.exe**; only use games and system files you have legally obtained. Games, keys and
firmware are not included.

### Existing saves and settings

On first launch TerraSwitch 2 copies your newest existing profile (an earlier TerraSwitch 2 or
Fritchii, Lichii, Eden, yuzu, suyu, Sudachi or Citron) into its own folder and shows what it copied.
Settings, keys, mods and saves are copied; caches are not, so shaders are prepared again once. The old
folder is never changed. A portable `user/` directory still takes priority. An interrupted copy is put
back and retried on the next launch.

## Compatibility and issue reports

Compatibility and performance vary by game, hardware and driver. Rendering errors, stutters and crashes
remain possible, and passing automated tests does not establish that a game renders correctly.

When [reporting an issue](https://github.com/TiktokTerra/TerraSwitch2/issues), include:

- The exact build, game version, CPU, GPU and driver version.
- The relevant settings and the steps to reproduce the problem.
- A screenshot and the log from the affected session, after checking it for personal information.

Do not upload game files, keys, firmware or private account information.

## Building from source

Requirements: CMake 3.31 or newer, a C++20 compiler, Ninja, Qt 6.8, and the platform dependencies
listed in the [dependencies guide](docs/Deps.md). The full walkthrough is in the
[build guide](docs/Build.md).

```sh
git clone https://github.com/TiktokTerra/TerraSwitch2.git
cd TerraSwitch2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build
```

The GUI build target is still named `yuzu` for historical reasons; its output is `terraswitch2.exe` on
Windows and `terraswitch2` on other desktop platforms. Android has its own
[build page](docs/build/Android.md).

Some inherited documentation and internal target names still use upstream names. When contributing,
keep changes focused, and distinguish code-level checks from actual gameplay testing; see the
[coding guidelines](docs/Coding.md).

## Credits

This project incorporates parts of the Litchii and Fritchii emulators, and inherits the work of the
Eglantine, Eden, yuzu and Sudachi projects and their contributors, including Eglantine's original
developer Camille LaVey. Thanks to all of them — see [credit.txt](credit.txt). Support the upstream
developers through the links on their project pages.

Launcher artwork is derived from the Lichii project's branding, drawn by YOB (yobslab), and is kept in
[dist/branding](dist/branding).

## License

TerraSwitch 2 is free software under the **GNU General Public License v3.0 or later**; see
[LICENSE.txt](LICENSE.txt). Individual files carry their own SPDX headers, and third-party licenses are
retained in [LICENSES](LICENSES).
