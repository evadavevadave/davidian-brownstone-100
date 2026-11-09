# Davidian Brownstone 100 — LV2 amp plugin

This is a mono Davidian LV2 guitar amp simulation of a classic late-1990s high-gain 120 W valve head, modeled stage by stage from its circuit. It includes a speaker cabinet simulation with four cabinet models, and a native GUI on Windows, macOS and Linux. Plugin URI: `urn:davidian:brownstone-100`.

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-brownstone-100/releases):

| Zip | For |
|---|---|
| `…-windows-x64.zip` | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | 64-bit ARM Linux, such as a Raspberry Pi 4 or 5 (glibc 2.35 or later) |

Unzip it, copy the `brownstone100.lv2` folder into your LV2 folder, and restart your host:

- Windows: `%APPDATA%\LV2` (create it if it doesn't exist) or `%COMMONPROGRAMFILES%\LV2`
- macOS: `~/Library/Audio/Plug-Ins/LV2`
- Linux: `~/.lv2`

The plugin isn't notarized by Apple, so macOS may block the downloaded files. After copying, run this once in Terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/brownstone100.lv2
```

The plugin works in hosts that load LV2 plugins, such as Reaper, Ardour, Carla and Qtractor.

## Build from source

The code is plain C99, with a small Objective-C window layer on macOS. It needs no LV2 SDK and no libraries beyond the system's own.

- **Windows (Visual Studio):** run `build.bat`. `build.bat test` also runs the tests, and `build.bat dist` makes a zip. The script sets up the Visual Studio environment itself.
- **Linux, macOS, BSD or MinGW:** run `make`. `make test` runs the offline tests, `make install` copies the bundle to your per-user LV2 folder, and `make dist` makes a zip.
  - Linux needs a C compiler and the X11 development headers (`sudo apt install build-essential libx11-dev` on Debian or Ubuntu).
  - macOS needs the Xcode command-line tools. `make` builds a universal binary for Apple silicon and Intel.

The bundle is built in `build/brownstone100.lv2/`.

Every push is built and tested on Windows, macOS and Linux (x64 and ARM) by GitHub Actions (`.github/workflows/build.yml`). Pushing a version tag such as `v1.0.0` publishes the four zips as a release.

## GUI

![Brownstone 100 panel](docs/gui.png)

The panel is called **Brownstone 100**. It uses the house GUI style (graphite panel, cream lettering, amber accents, black knurled knobs, red footswitch LED). The style lives in `src/ui/style.h`, `font.h` and `canvas.h`. The panel has its own layout and name, and no branding from the original amp.

- **Knobs:** drag up or down. Hold Shift for fine control, use the mouse wheel for small steps, and double-click to reset.
- **Toggle switches:** click the position you want.
- **Cab Sim footswitch:** turns the cabinet simulation on or off. The red LED is lit when it's on.
- **Cabinet model selector:** click a cabinet's name, or drag the knob up or down. Its pointer aims at the chosen name.
- **Readout:** the window at the top right shows the control you last touched.

The GUI draws itself with its own software renderer, so it needs no graphics libraries. It opens a native window: `ui:WindowsUI` on Windows, `ui:CocoaUI` on macOS and `ui:X11UI` on Linux.

## Controls

| Control | Notes |
|---|---|
| Channel | Rhythm (with the K4B shunt), Rhythm Crunch (S1C), Lead (K1/K2/K4 energized) |
| Rhythm Pre / Post, Lead Pre / Post | 1M pots VR1/VR6 and VR2/VR7. Post sets how hard the power amp is driven (the original amp is very loud past 3) |
| Low / Mid / High | FMV tone stack: 1M / 50k / 250k, 47k slope, 470p / 22n / 22n |
| Presence / Resonance | Reduce high- or low-frequency negative feedback in the power amp |
| Low Input | J39 input (−6 dB) |
| Cabinet Sim | Turns the speaker cabinet simulation on or off. Turn it off to use your own impulse response |
| Cabinet Model | 4x12 Modern, 4x12 Vintage, 2x12 Open Back or 1x12 Combo (see below) |
| Input Trim / Output | 0 dBFS at the input counts as 1 V peak at the guitar jack |

## Cabinet models

| Model | Character |
|---|---|
| 4x12 Modern | Closed back. Tight low end, scooped low mids, forward upper mids. This is the voicing from before the selector existed, so older sessions sound the same |
| 4x12 Vintage | Closed back. Darker and smoother on top, mid-forward, like lower-power vintage-style speakers |
| 2x12 Open Back | Less low-end thump, a cancellation dip in the low mids, more air |
| 1x12 Combo | Small open-back box. Tight bass, boxy low mids, bright upper mids |

Each model is a cascade of seven filters (high-pass, resonance and presence peaks, low-pass roll-off) in `src/cabinet.h`. The models are generic voicings, not measurements of any particular cabinet or speaker. They are loudness-matched to each other, so switching changes the tone, not the level. Switching models, or turning the cab on or off, crossfades over 20 ms so it doesn't click.

## What is modeled

- **Tubes:** each 12AX7 stage (V1A, V1B, V2A, V2B, V5B, V5A, V3A) uses the Koren triode equations solved on that stage's AC load line, using the circuit's plate, cathode and load resistors. This includes:
  - cutoff and saturation
  - grid conduction
  - frequency-dependent gain from the cathode bypass capacitor
  - the 39k "cold clipper" on V2A
- **Coupling network filters:** the coupling caps, the R11/C48 2 kHz roll-off, and the grid-stopper/Miller-capacitance low-pass filters (R82 470k, R7 220k, R88 1M).
- **Rest of the amp:**
  - V3B cathode follower
  - Yeh's exact tone stack equations
  - Long-tail phase inverter
  - 6L6GC push-pull class AB stage
  - Output transformer bandwidth
  - The negative feedback loop, solved implicitly every sample
- **Oversampling:** everything that distorts runs at 4x with 12th-order anti-alias filters.

## Approximations

- The relay and switch wiring around K4 and S1C isn't fully clear. These parts are reduced to their main effect:
  - In Rhythm, K4B shunts R23 39k + C63 0.1µF to ground (about −30 dB).
  - Crunch removes that shunt.
- These parts are behavioral models, not a component-level circuit solution:
  - The power section, which has no sag
  - The phase inverter
  - The cabinet models
- The Pre/Post pots use a steep audio taper (the pots are marked "A3"). The rhythm-channel levels were tuned so the channel stays clean at Pre ≈ 1–2 and breaks up from about 4–5.

## Files

- `src/brownstone100.c`: the DSP code and LV2 entry point
- `src/cabinet.h`: the cabinet models and their crossfade
- `src/ports.h`: port indices and URIs shared by the plugin and the GUI
- `src/lv2_core.h`: minimal LV2 ABI. Build with `-DHAVE_LV2` to use the system headers instead
- `src/ui/`: the GUI
  - `panel.h`: layout, drawing and mouse handling
  - `ui_lv2.c`: LV2 UI entry points and the Win32, Cocoa and X11 window layers
  - `style.h`, `font.h`, `canvas.h`: the house style
- `brownstone100.lv2/`: the TTL metadata
- `Makefile`, `build.bat`: builds for Linux/macOS/MinGW and for Visual Studio
- `.github/workflows/build.yml`: builds, tests and releases on all three platforms
- `test/render.c`: offline DSP test. It renders a synthetic power-chord DI through each channel and each cabinet model, checks for NaN, feedback-loop stability, cabinet loudness matching and click-free cabinet switching, and writes `build-test/test_*.wav`
- `test/ui_render.c`: renders the panel to a BMP without opening a window
- `test/ui_check.c`: loads the GUI the way a Windows host does, clicks and drags the controls, and checks the values sent back (Windows)
- `test/ui_check_mac.m`: the same for the Cocoa GUI, and also checks the panel is drawn the right way up (macOS)
- `test/load_check.c`: loads the plugin and GUI with `dlopen`, runs audio through the plugin, and opens the X11 GUI on a virtual display (Linux, macOS)
- `LICENSE`: GPL-3.0 text. `LICENSE-LV2`: the ISC notice for the re-declared LV2 interface

## References

The model re-implements equations from these published works. No code was copied from them.

1. N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996. The triode equation and the 12AX7 parameters (μ = 100, Ex = 1.4, Kg1 = 1060, Kp = 600, Kvb = 300).
2. D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender Bassman tone stack", *Proc. 9th Int. Conf. on Digital Audio Effects (DAFx-06)*, Montreal, 2006. The analytic tone stack transfer function and its bilinear discretization.
3. R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the Audio EQ Cookbook). The low-pass, high-pass and peaking filters in the anti-alias and cabinet filters.
4. V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, 2012–2020. The topology-preserving one-pole filters, and the instantaneous response used to solve the power-amp feedback loop.
5. K. Karplus and A. Strong, "Digital synthesis of plucked-string and drum timbres", *Computer Music Journal* 7(2), 1983. Only used to make the test signal in `test/render.c`.

## Use of AI

This project was made with AI assistance. The code, tests, LV2 metadata, build scripts, release workflow and this README were written by Claude (Anthropic's Claude Opus 5.5, through Claude Code), directed and reviewed by the author.

- **The author** supplied the circuit, the GUI house style, the names and the licensing choices. They also decided what to build and changed parts of the result themselves, including renaming the project to Brownstone 100.
- **The AI**:
  - read component values and topology from the circuit and chose which parts to simplify (see [Approximations](#approximations))
  - wrote the DSP model and set the rhythm-channel and pot-taper calibration
  - designed the four cabinet voicings
  - adapted the house-style GUI to this panel and wrote the macOS window layer
  - wrote the test programs, the cross-platform build and release workflow, and the documentation
- **Checking:** the results are checked by the automated tests in `test/`, which GitHub Actions runs on Windows, macOS and Linux:
  - numerical stability of the DSP, levels and speed
  - cabinet loudness matching and click-free switching at 44.1 to 192 kHz
  - each GUI loaded and driven by simulated mouse input (Windows, macOS), or opened on a virtual display (Linux)
  - the bundle read and run by the reference LV2 host library, lilv (Linux)
- **Not checked:** the sound hasn't been compared with a real amplifier or cabinet. The calibrated values (channel attenuations, pot tapers, cabinet voicings, power-amp constants) are the AI's estimates. They should be tuned by ear. The macOS and Linux builds haven't been tried in a real host by the author.

## License

Copyright (C) 2026 Davidian

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text.

## Third-party licences

| Component | Licence | How it's used |
|---|---|---|
| LV2 C interface (`lv2/core/lv2.h`, `lv2/ui/ui.h`) | ISC | Its declarations are re-stated in `src/lv2_core.h`, the UI ABI block of `src/ui/ui_lv2.c` and the test programs, so no LV2 SDK is needed. Copyright and permission notice in [LICENSE-LV2](LICENSE-LV2). |
| LV2 vocabularies (`lv2core`, `ui`, `units`, `port-props`) | ISC | The `.ttl` files refer to them by URI. Nothing is copied. |
| Microsoft C runtime | Visual Studio licence | `build.bat` links it statically (`/MT`) into the Windows DLLs. It counts as a system library under the GPL, so this is allowed. |
| Xlib (Linux) | MIT/X11 | The Linux GUI links to the system's `libX11` at run time. Not included in the release zips. |
| Cocoa (macOS) | Apple system framework | The macOS GUI links to it at run time, as a system library. Not included. |
| lilv, Xvfb, GitHub Actions | Various | Used only to test and build on GitHub's machines. Nothing from them is included. |

Every source file starts with an SPDX licence line saying which of these applies.

This project is not affiliated with or endorsed by the maker of the original amplifier. Product names in cited paper titles are trademarks of their owners.
