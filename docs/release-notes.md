Ready-made builds of the **Davidian Brownstone 100** LV2 amp plugin, with its GUI.

| Download | For |
|---|---|
| `…-windows-x64.zip` | Windows 10/11, 64-bit hosts |
| `…-macos-universal.zip` | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x64.zip` | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64.zip` | 64-bit ARM Linux, e.g. Raspberry Pi 4/5 (glibc 2.35 or later) |

**Install:** unzip, then copy the `brownstone100.lv2` folder into your LV2 folder and restart your host:

- Windows: `%APPDATA%\LV2` (create it if it doesn't exist)
- macOS: `~/Library/Audio/Plug-Ins/LV2`
- Linux: `~/.lv2`

**macOS:** the plugin isn't notarized, so macOS may block files downloaded from the internet. After copying, run this once in Terminal:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/brownstone100.lv2
```

See the README for the controls, the cabinet models and what is modeled.
