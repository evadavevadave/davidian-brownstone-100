Ready-made builds of the **Davidian Brownstone 100** amp plugin, with its GUI, in every format each system supports.

| Download | Formats | For |
|---|---|---|
| `…-windows-x64.zip` | LV2, VST3 | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | LV2, VST3, Audio Unit | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | LV2, VST3 | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | LV2, VST3 | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 (glibc 2.35 or later) |

**Install:** unzip, copy the format your host uses into its plugin folder, and restart the host. `INSTALL.txt` in the zip lists the folders:

- LV2: Windows `%APPDATA%\LV2`, macOS `~/Library/Audio/Plug-Ins/LV2`, Linux `~/.lv2`
- VST3: Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`, Linux `~/.vst3`
- Audio Unit (macOS): `~/Library/Audio/Plug-Ins/Components`

**macOS:** the plugin isn't notarized, so macOS may block files downloaded from the internet. After copying, run this once in Terminal for each format you installed, for example:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/brownstone100.component
```

**New in v1.1**

- VST3 (Windows, macOS, Linux) and Audio Unit (macOS) versions, with the same sound and panel as the LV2.
- The circuit model was re-checked against the schematic and published analyses, and corrected: V5A's shunt feedback, the tone stack's real drive and loading, the Crunch switch, the Rhythm/Lead cathode bypass, and the real Presence and Resonance network. The sound and gain staging change noticeably from v1.0.
- Rhythm defaults are now Pre 4 / Post 6. Saved v1.0 sessions keep their settings, but sound different because of the corrected model.

See the README for the controls, the cabinet models and what is modeled.
