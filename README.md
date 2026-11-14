# Davidian Brownstone 100

A circuit-modelled guitar amplifier plugin (LV2, VST3 and Audio Unit), modelled stage by stage on the circuit of a classic late-1990s high-gain 120 W valve head: a two-channel 12AX7 preamp with Rhythm, Crunch and Lead modes, an FMV tone stack, a long-tail phase inverter and a 6L6GC push-pull power amp with presence and resonance, plus a speaker cabinet simulation with four cabinet models.

**A close model, not an exact copy.** It is a simulation of the circuit from its component values, not a sampled or measured amp, and some details are reasoned estimates (listed under [Estimates](#estimates)). It has not been compared side by side with a built unit, so expect it to sound close to the hardware rather than identical.

![The GUI](docs/gui.png)

A mono amp (mono or stereo tracks work too), with the same GUI on every system. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |
| VST3 | yes | yes | yes |
| Audio Unit | — | yes | — |

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-brownstone-100/releases):

| Download | Contains | For |
|---|---|---|
| `…-windows-x64.zip` | LV2, VST3 | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | LV2, VST3, AU | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | LV2, VST3 | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | LV2, VST3 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 (glibc 2.35 or later) |

Unzip, copy the format your host uses into its plugin folder, and restart your host (or rescan plugins). `INSTALL.txt` in the zip has the same steps.

| Format | Copy | Into |
|---|---|---|
| LV2 (Windows) | `LV2/brownstone100.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist) or `%COMMONPROGRAMFILES%\LV2` |
| LV2 (macOS) | `LV2/brownstone100.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `LV2/brownstone100.lv2` | `~/.lv2` |
| VST3 (Windows) | `VST3/brownstone100.vst3` | `C:\Program Files\Common Files\VST3` |
| VST3 (macOS) | `VST3/brownstone100.vst3` | `~/Library/Audio/Plug-Ins/VST3` |
| VST3 (Linux) | `VST3/brownstone100.vst3` | `~/.vst3` |
| Audio Unit (macOS) | `AU/brownstone100.component` | `~/Library/Audio/Plug-Ins/Components` |

In the host it is listed as **Davidian Brownstone 100**. The LV2 URI is `urn:davidian:brownstone-100`; the Audio Unit is type `aufx`, subtype `Bs1h`, manufacturer `Dvdn`. LV2 works in hosts such as Reaper, Ardour, Carla and Qtractor; VST3 in most DAWs, including Reaper, Cubase, Ableton Live, Bitwig and FL Studio; Audio Unit in Logic Pro, GarageBand and other macOS hosts.

The macOS build isn't notarized, so macOS may block it after download. Run these once in Terminal after copying, for each format you installed:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/brownstone100.lv2
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/brownstone100.vst3
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/brownstone100.component
```

## Controls

| Panel | Control | Circuit part |
|---|---|---|
| Rhythm | Pre, Post | 1M pots VR1 (Clean Pre, with the C27 bright cap) and VR6 (Clean Post). Post sets how hard the power amp is driven (the original amp is very loud past 3) |
| Lead | Pre, Post | 1M pots VR2 (Ultra Pre, with R40 1M and C19 1n across it) and VR7 (Ultra Post) |
| Tone | Low, Mid, High | Tone stack: VR3 1M / VR4 50k / VR5 250k, R28 47k slope, C14 470p ∥ C60 100p, C18 and C21 22n, fed through R89 470k into R24 33k |
| Input | Jack: High / Low -6 | The J39 low input (−6 dB) |
| Input | Trim | Input trim: 0 dBFS at the input counts as 1 V peak at the guitar jack |
| Channel | Select: Rhythm / Crunch / Lead | Rhythm (relays at rest: K4B shunt R23 39k + C63, 22µF V1B bypass, R20 shunt at V2A), Crunch (S1C connects C10 470p across R21: a treble and upper-mid boost), Lead (K1, K2 and K4 energized) |
| Power Amp | Presence, Resonance | Presence: C8 0.1µ, C62 0.033µ and VR9 10k shunt treble out of the feedback. Resonance: VR8 1M ∥ C12 6.8n in series with the feedback cuts its bass. Both feed R56 39k into the phase inverter's tail |
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
- **V5A** is a shunt-feedback stage: R88 1M feeds the plate signal (after C58) back to the grid, with R87 1M in and R91 1M to ground, so it runs at close to unity gain. Its grid voltage is solved every sample.
- **Passive networks** are simulated as circuits with their real part values (nodal analysis with trapezoidal capacitors, `src/linnet.h`), so the pots, loads and source impedances interact as in the amp:
  - the channel networks after C6, including the Pre pots, bright caps, the K4B shunt and the Crunch switch
  - the tone stack, fed from V5A through C58 and R89 470k, loaded by R24 33k, C59 + R94 and both Post pots
  - the presence and resonance network in the power amp's feedback
- **Coupling filters:** the coupling caps, the R11/C48 2 kHz roll-off, and the grid-stopper/Miller-capacitance low-pass filters (R82 470k, R7 220k).
- **Rest of the amp:** V3B as a cathode follower after the Post pots, V3A, the long-tail phase inverter (tail R52 470 + R51 10k + R57 4.7k), the 6L6GC push-pull class AB stage, the output transformer's bandwidth, and the negative feedback loop through R56 39k, solved implicitly every sample.
- **Oversampling:** everything that distorts runs at 4x with 12th-order anti-alias filters.

### Checked against sources

In v1.1 every stage was re-read from the full-resolution schematic and cross-checked with published analyses of the circuit (see [References](#references)). This corrected these parts of v1.0:

| v1.0 | Now |
|---|---|
| V5A as a high-gain stage behind a 1M grid stopper (R88) | R88 is plate-to-grid shunt feedback; V5A runs at about unity gain |
| A cathode follower driving the tone stack | V5A drives the stack through R89 470k into a 33k load (about −24 dB); V3B buffers after the Post pots |
| 470p treble cap | C14 470p ∥ C60 100p |
| 22µF V1B cathode bypass in Lead | In Rhythm: K1A grounds C26 with the relays at rest |
| Crunch removed the K4B shunt (+24 dB) | Crunch connects C10 470p across R21: a treble and upper-mid boost |
| Presence and Resonance as generic shelves | The real C8 / C62 / VR9 and C12 ∥ VR8 network into R56 39k |
| Preamp supplies 330 / 300 V | 300 / 280 V |
| Steep taper on the Post pots, to make up for the missing stack loss | Normal audio taper |
| 100 W on the panel | 120 W |

The new values also fixed a solver fault: under heavy drive, V5A's feedback solve could cycle and add low tones that weren't in the input. A test now checks for them.

### Checked by the tests

`render` runs a synthetic power-chord DI through each channel and each cabinet model and checks for NaN, feedback-loop stability, spurious low tones under heavy drive, cabinet loudness matching and click-free cabinet switching at 44.1 to 192 kHz. The model has not been checked against voltages measured on a real amp.

### Estimates

The schematic doesn't settle these, so they are reasoned estimates:

- The supply voltages: the schematic gives none. 300 V (V1) and 280 V (V2) follow a forum member's reading of the related combo's schematic (see [References](#references)).
- The pot tapers. The Pre pots (marked "A3") use a steep audio taper, about 3% at mid travel; the Post, Low and Resonance pots a normal audio taper (10%); High, Mid and Presence are linear.
- The output impedances of V1A (48k) and V5A (8k) where they drive the simulated networks, and the phase inverter's tail load (3.3k).
- The power-amp constants (phase-inverter gain and swing, 6L6GC bias) and the cabinet voicings.

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
| `src/linnet.h` | Circuit simulator for the passive networks |
| `src/cabinet.h` | The cabinet models and their crossfade |
| `plugin/` | VST3 and Audio Unit wrapper (DPF): plugin, GUI and plugin description |
| `CMakeLists.txt` | VST3 and Audio Unit build |
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

### VST3 and Audio Unit (all platforms)

The VST3 and Audio Unit versions use the [DISTRHO Plugin Framework](https://github.com/DISTRHO/DPF) (DPF, ISC licence), which CMake fetches at a pinned commit, so it needs Git, CMake 3.17 or later and an internet connection the first time. They run the same DSP code as the LV2 version, and show the same panel through OpenGL. On Linux, also install the OpenGL and X11 headers (`libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev` on Debian/Ubuntu).

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-cmake --config Release
```

This makes `build-cmake/bin/brownstone100.vst3` and, on macOS, `build-cmake/bin/brownstone100.component` (a universal binary). To use a local DPF checkout instead, add `-DDPF_ROOT=/path/to/DPF`. `ci/package.sh` zips every built format, as the release workflow does.

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

GitHub Actions runs all of these on Windows, macOS, Linux x64 and Linux arm64 for every push (see `.github/workflows/build.yml`). It also builds the VST3 and Audio Unit versions and checks them:

- `pluginval` (Tracktion's plugin validator, strictness level 5) loads and tests the VST3 on Windows, macOS and Linux x64, and the Audio Unit on macOS.
- Apple's `auval` validates the Audio Unit in strict mode.

Pushing a version tag such as `v1.1.0` publishes a release with a zip for each platform.

## Use of AI

Made with generative AI (Claude Code, Anthropic's Claude Opus 5.5), directed and reviewed by Davidian. The code, tests, LV2 metadata, build scripts, release workflow and this README were written by the AI.

- **The author** supplied the circuit, the GUI house style, the names and the licensing choices. They also decided what to build and changed parts of the result themselves, including renaming the project to Brownstone 100.
- **The AI**:
  - read component values and topology from the circuit and chose which parts to simplify (see [Estimates](#estimates) and [Simplifications](#simplifications))
  - re-checked every stage against the full-resolution schematic and published analyses, and corrected the model (see [Checked against sources](#checked-against-sources))
  - wrote the DSP model, the circuit simulator for the passive networks, and the pot-taper calibration
  - designed the four cabinet voicings
  - adapted the house-style GUI to this panel and wrote the macOS window layer
  - wrote the VST3 and Audio Unit wrapper
  - wrote the test programs, the cross-platform build and release workflow, and the documentation
- **Checking:** the results are checked by the automated tests in `test/`, which GitHub Actions runs on Windows, macOS and Linux:
  - numerical stability of the DSP, levels and speed
  - cabinet loudness matching and click-free switching at 44.1 to 192 kHz
  - each GUI loaded and driven by simulated mouse input (Windows, macOS), or opened on a virtual display (Linux)
  - the bundle read and run by the reference LV2 host library, lilv (Linux)
  - the VST3 and Audio Unit validated by pluginval and auval
- **Not checked:** the sound hasn't been compared with a real amplifier or cabinet. The values listed under [Estimates](#estimates) are the AI's. They should be tuned by ear. The macOS and Linux builds, and the VST3 and Audio Unit versions, haven't been tried in a real host by the author.

## References

The model re-implements equations from these published works. No code was copied from them.

- N. Koren, "Improved vacuum tube models for SPICE simulations", *Glass Audio* 8(5), 1996. The triode equation and the 12AX7 parameters (μ = 100, Ex = 1.4, Kg1 = 1060, Kp = 600, Kvb = 300).
- C.-W. Ho, A. E. Ruehli and P. A. Brennan, "The modified nodal approach to network analysis", *IEEE Transactions on Circuits and Systems* 22(6), 1975. The nodal analysis used to simulate the passive networks.
- R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients" (the Audio EQ Cookbook). The low-pass, high-pass and peaking filters in the anti-alias and cabinet filters.
- V. Zavalishin, *The Art of VA Filter Design*, Native Instruments, 2012–2020. The topology-preserving one-pole filters, and the instantaneous response used to solve the power-amp feedback loop.
- K. Karplus and A. Strong, "Digital synthesis of plucked-string and drum timbres", *Computer Music Journal* 7(2), 1983. Only used to make the test signal in `test/render.c`.

These were used to cross-check the circuit reading in v1.1:

- [OpenGuitarMultiFx pull request #161](https://github.com/LuhOli42/OpenGuitarMultiFx/pull/161) (Oct 2026), a stage-by-stage reading of the same circuit for another model. It agrees on the V5A shunt feedback, the 33k-loaded stack input, the in-series R29 and the 39k cold-clipper cathode, the 47k slope resistor and the phase inverter's tail.
- [Music Electronics Forum thread on the combo version's chassis](https://music-electronics-forum.com/forum/amplification/guitar-amps/maintenance-troubleshooting-repair/48259-5150-combo-chassis), where a forum member reads the supply nodes off the schematic of the related combo as 440, 300 and 280 V (the source of the 300 / 280 V preamp estimate). It is a reading, not a measurement.

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
| DISTRHO Plugin Framework (DPF) and its pugl | ISC | Fetched at a pinned commit when building the VST3 and Audio Unit versions, and compiled into them. Copyright and permission notices in [LICENSE-DPF](LICENSE-DPF), which is in every release zip. Not used by the LV2 version. |
| VST3 and Audio Unit interfaces | — | DPF implements them itself, so no Steinberg or Apple SDK is downloaded. The Audio Unit links to Apple's system frameworks. VST is a registered trademark of Steinberg Media Technologies GmbH. |
| lilv, Xvfb, pluginval, GitHub Actions | Various | Used only to test and build on GitHub's machines. Nothing from them is included. |

This project is not affiliated with or endorsed by the maker of the original amplifier. Product names in cited paper titles are trademarks of their owners.
