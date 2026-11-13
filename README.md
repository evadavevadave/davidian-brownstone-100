# Davidian Brownstone 100

A circuit-modelled guitar amplifier plugin (LV2), modelled stage by stage on the circuit of a classic late-1990s high-gain 120 W valve head: a two-channel 12AX7 preamp with Rhythm, Crunch and Lead modes, an FMV tone stack, a long-tail phase inverter and a 6L6GC push-pull power amp with presence and resonance, plus a speaker cabinet simulation with four cabinet models.

**A close model, not an exact copy.** It is a simulation of the circuit from its component values, not a sampled or measured amp, and some details are reasoned estimates (listed under [Estimates](#estimates)). It has not been compared side by side with a built unit, so expect it to sound close to the hardware rather than identical.

![The GUI](docs/gui.png)

A mono amp (mono or stereo tracks work too), with the same GUI on every system. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-brownstone-100/releases):

| Download | Contains | For |
|---|---|---|
| `…-windows-x64.zip` | LV2 | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | LV2 | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | LV2 | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | LV2 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 (glibc 2.35 or later) |

Unzip, copy the plugin folder into your LV2 folder, and restart your host (or rescan plugins):

| Format | Copy | Into |
|---|---|---|
| LV2 (Windows) | `brownstone100.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) or `%COMMONPROGRAMFILES%\LV2` |
| LV2 (macOS) | `brownstone100.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `brownstone100.lv2` | `~/.lv2` |

In the host it is listed as **Davidian Brownstone 100**. It works in hosts that load LV2 plugins, such as Reaper, Ardour, Carla and Qtractor. The LV2 URI is `urn:davidian:brownstone-100`.

The macOS build isn't notarized, so macOS may block it after download. Run this once in Terminal after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/brownstone100.lv2
```

## Controls

| Panel | Control | Circuit part |
|---|---|---|
| Rhythm | Pre, Post | 1M pots VR1 and VR6. Post sets how hard the power amp is driven (the original amp is very loud past 3) |
| Lead | Pre, Post | 1M pots VR2 and VR7 |
| Tone | Low, Mid, High | FMV tone stack: 1M / 50k / 250k, 47k slope, 470p / 22n / 22n |
| Input | Jack: High / Low -6 | The J39 low input (−6 dB) |
| Input | Trim | Input trim: 0 dBFS at the input counts as 1 V peak at the guitar jack |
| Channel | Select: Rhythm / Crunch / Lead | Rhythm (with the K4B shunt), Rhythm Crunch (S1C), Lead (K1/K2/K4 energized) |
| Power Amp | Presence, Resonance | Reduce high- or low-frequency negative feedback in the power amp |
| Output | Level | Output level |
| Cabinet | Footswitch | Not on the amp: turns the speaker cabinet simulation on or off; the red LED is lit when it's on. Turn it off to use your own impulse response |
| Cabinet | Model | Not on the amp: 4x12 Modern, 4x12 Vintage, 2x12 Open Back or 1x12 Combo (see [Cabinets](#cabinets)). Click a cabinet's name, or drag the knob; its pointer aims at the chosen name |

The readout at the top right shows the control you last touched.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. Click the position you want on a toggle switch.

### Cabinets

| Model | Character |
|---|---|
| 4x12 Modern | Closed back. Tight low end, scooped low mids, forward upper mids. This is the voicing from before the selector existed, so older sessions sound the same |
| 4x12 Vintage | Closed back. Darker and smoother on top, mid-forward, like lower-power vintage-style speakers |
| 2x12 Open Back | Less low-end thump, a cancellation dip in the low mids, more air |
| 1x12 Combo | Small open-back box. Tight bass, boxy low mids, bright upper mids |

Each model is a cascade of seven filters (high-pass, resonance and presence peaks, low-pass roll-off) in `src/cabinet.h`. The models are generic voicings, not measurements of any particular cabinet or speaker. They are loudness-matched to each other, so switching changes the tone, not the level. Switching models, or turning the cab on or off, crossfades over 20 ms so it doesn't click.

## How it is modelled

In `src/brownstone100.c`, with the circuit's part values.

- **Tubes:** each 12AX7 stage (V1A, V1B, V2A, V2B, V5B, V5A, V3A) uses the Koren triode equations solved on that stage's AC load line, using the circuit's plate, cathode and load resistors. This includes cutoff and saturation, grid conduction, frequency-dependent gain from the cathode bypass capacitor, and the 39k "cold clipper" on V2A.
- **Coupling network filters:** the coupling caps, the R11/C48 2 kHz roll-off, and the grid-stopper/Miller-capacitance low-pass filters (R82 470k, R7 220k, R88 1M).
- **Rest of the amp:** the V3B cathode follower, the tone stack (Yeh's exact equations), the long-tail phase inverter, the 6L6GC push-pull class AB stage, the output transformer's bandwidth, and the negative feedback loop, solved implicitly every sample.
- **Oversampling:** everything that distorts runs at 4x with 12th-order anti-alias filters.

### Checked by the tests

`render` runs a synthetic power-chord DI through each channel and each cabinet model and checks for NaN, feedback-loop stability, cabinet loudness matching and click-free cabinet switching at 44.1 to 192 kHz. The model has not been checked against measured voltages from the schematic.

### Estimates

The circuit doesn't settle these, so they are reasoned estimates:

- The relay and switch wiring around K4 and S1C isn't fully clear, so it is reduced to its main effect: in Rhythm, K4B shunts R23 39k + C63 0.1µF to ground (about −30 dB), and Crunch removes that shunt.
- The Pre/Post pots use a steep audio taper (the pots are marked "A3"). The rhythm-channel levels were tuned so the channel stays clean at Pre ≈ 1–2 and breaks up from about 4–5.
- The power-amp constants and the cabinet voicings.

### Simplifications

These parts are behavioural models, not a component-level circuit solution:

- The power section, which has no sag.
- The phase inverter.
- The cabinet models.

## Build

The code is plain C99 (Objective-C for the macOS window layer) with no dependencies beyond the platform's own libraries: Win32 on Windows, Cocoa on macOS, Xlib on Linux. No LV2 SDK is needed; build with `-DHAVE_LV2` to use the system headers instead.

| Source | What it is |
|---|---|
| `src/brownstone100.c` | The amp model and LV2 entry point |
| `src/cabinet.h` | The cabinet models and their crossfade |
| `src/ports.h` | Port indices and URIs shared by the plugin and the GUI |
| `src/ui/ui_lv2.c` | LV2 GUI entry points and the Win32, Cocoa and X11 window layers |
| `src/ui/panel.h` | The panel: layout, drawing and mouse handling |
| `src/ui/style.h`, `font.h`, `canvas.h` | Davidian GUI house style: palette, stroke font and renderer |
| `src/lv2_core.h` | The LV2 C interface, so no SDK is needed |
| `brownstone100.lv2/` | LV2 metadata |

### Windows (Visual Studio)

Needs Visual Studio with the C++ tools. `build.bat` sets up the Visual Studio environment itself, and builds `build\brownstone100.lv2`. `build.bat dist` makes a zip.

```bash
build.bat
```

### macOS, Linux, BSD, MinGW

Needs a C compiler and `make`. On Linux, also the X11 headers (`build-essential libx11-dev` on Debian/Ubuntu). On macOS, the Xcode command-line tools; `make` builds a universal binary for Apple silicon and Intel.

```bash
make
```

```bash
make install
```

`make install` copies the bundle to your per-user LV2 folder, and `make dist` makes a zip.

## Tests

Windows:

```bash
build.bat test
```

macOS, Linux:

```bash
make test
```

- `render` renders a synthetic power-chord DI through each channel and each cabinet model, checks for NaN, feedback-loop stability, levels, speed, cabinet loudness matching and click-free cabinet switching, and writes `build-test/test_*.wav`.
- `ui_render` draws the panel to a BMP file without opening a window.
- `ui_check` (Windows) loads the GUI the way a Windows host does, clicks and drags the controls, and checks the values sent back.
- `ui_check_mac` (macOS) does the same for the Cocoa GUI, and also checks the panel is drawn the right way up.
- `load_check` (Linux, macOS) loads the plugin and GUI with `dlopen`, runs audio through the plugin, and opens the X11 GUI on a virtual display. On Linux, CI also reads and runs the bundle with lilv.

GitHub Actions runs all of these on Windows, macOS, Linux x64 and Linux arm64 for every push (see `.github/workflows/build.yml`). Pushing a version tag such as `v1.0.0` publishes a release with a zip for each platform.

## Use of AI

Made with generative AI (Claude Code, Anthropic's Claude Opus 5.5), directed and reviewed by Davidian. The code, tests, LV2 metadata, build scripts, release workflow and this README were written by the AI.

- **The author** supplied the circuit, the GUI house style, the names and the licensing choices. They also decided what to build and changed parts of the result themselves, including renaming the project to Brownstone 100.
- **The AI**:
  - read component values and topology from the circuit and chose which parts to simplify (see [Estimates](#estimates) and [Simplifications](#simplifications))
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

## References

The model re-implements equations from these published works. No code was copied from them.

- N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996. The triode equation and the 12AX7 parameters (μ = 100, Ex = 1.4, Kg1 = 1060, Kp = 600, Kvb = 300).
- D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender Bassman tone stack", *Proc. 9th Int. Conf. on Digital Audio Effects (DAFx-06)*, Montreal, 2006. The analytic tone stack transfer function and its bilinear discretization.
- R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the Audio EQ Cookbook). The low-pass, high-pass and peaking filters in the anti-alias and cabinet filters.
- V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, 2012–2020. The topology-preserving one-pole filters, and the instantaneous response used to solve the power-amp feedback loop.
- K. Karplus and A. Strong, "Digital synthesis of plucked-string and drum timbres", *Computer Music Journal* 7(2), 1983. Only used to make the test signal in `test/render.c`.

## Licence

Copyright (C) 2026 Davidian. GPL-3.0-or-later: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [LICENSE](LICENSE) for the full text. Every source file starts with an SPDX licence line saying which licence applies to it.

### Third-party licences

| Component | Licence | How it's used |
|---|---|---|
| LV2 C interface (`lv2/core/lv2.h`, `lv2/ui/ui.h`) | ISC | Its declarations are re-stated in `src/lv2_core.h`, the UI ABI block of `src/ui/ui_lv2.c` and the test programs, so no LV2 SDK is needed. Copyright and permission notice in [LICENSE-LV2](LICENSE-LV2). |
| LV2 vocabularies (`lv2core`, `ui`, `units`, `port-props`) | ISC | The `.ttl` files refer to them by URI. Nothing is copied. |
| Microsoft C runtime | Visual Studio licence | `build.bat` links it statically (`/MT`) into the Windows DLLs. It counts as a system library under the GPL, so this is allowed. |
| Xlib (Linux) | MIT/X11 | The Linux GUI links to the system's `libX11` at run time. Not included in the release zips. |
| Cocoa (macOS) | Apple system framework | The macOS GUI links to it at run time, as a system library. Not included. |
| lilv, Xvfb, GitHub Actions | Various | Used only to test and build on GitHub's machines. Nothing from them is included. |

This project is not affiliated with or endorsed by the maker of the original amplifier. Product names in cited paper titles are trademarks of their owners.
