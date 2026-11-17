# PhoneVR: simple SteamVR phone headset

Simple PhoneVR implementation similar to already existing ones. Difference is, the PC does everything: SteamVR renders, the driver applies the lens distortion, converts to
NV12 on the GPU and hardware-encodes H.264. The phone only hardware-decodes into a plain
SurfaceView and streams its rotation sensor. **No Google Cardboard API, no OpenGL, no VR SDK**
on the phone, so there is almost no load or heat on it. I was unsatisfied with existing implementations cooking my phone so I made this instead.

```
SteamVR compositor (direct mode)
  -> driver_phonevr.dll: D3D11 lens pre-distortion (side-by-side) -> RGB->NV12 (GPU)
  -> Media Foundation hardware H.264 (NVENC / AMF / QuickSync)
  -> TCP  ------------------------------------------>  Android MediaCodec -> SurfaceView
  <- rotation quaternion @ ~330 Hz, recenter, bitrate, IDR requests
```

* **Transport**: one TCP connection (video + sensors).  If the link falls
behind, the host drops queued frames and resynchronizes on a fresh keyframe instead of building latency.
  * USB: the driver runs `adb reverse tcp:9943 tcp:9943` automatically; the phone connects to 127.0.0.1.
  * WiFi: UDP broadcast discovery (port 9944), or type the PC's IP in the app.
* **Compression is adjustable**: `bitrateKbps` in the settings file, or the slider in the phone app
  (changes the encoder live, CBR). Resolution/refresh are in the settings file.
* **3DoF** head tracking with `GAME_ROTATION_VECTOR` (gyro+accelerometer fusion done by Android).
  SteamVR also applies a small neck model.
* **Recentering**: volume down or double-tap on the phone, `Ctrl+Alt+R` on the PC, and automatically on
  connect. (SteamVR's own "Recenter" also works.)
* **Quick Brightness adjust**: volume up to cycle through 5 brightness steps of 10%, 25%, 45%, 70% and 100%.
* **Holder support**: side-by-side barrel-distorted output with adjustable `k1/k2`, chromatic
  correction, lens-centre shift and FOV. Both landscape directions are supported.

## Layout

| Path | What |
|---|---|
| `host/src/` | SteamVR driver |
| `host/package/phonevr/` | Driver manifest + default settings |
| `host/install.ps1` | One-step install/registration with SteamVR |
| `android/` | Kotlin app |

## Prerequisites

1. Install [Microsoft Visual C++ Redistributables](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170#latest-supported-redistributable-version).
2. Install SteamVR and run it at least once to generate default configs. It's fine if you have already run it with a different HMD before. The setup script will make a backup of your previous configs.
3. Make sure you are connected to the Internet before running the PC setup since this will download dependencies it needs.

## Installation (PC, Windows 10/11)

1. Navigate to Releases and download `PhoneVR-Setup.exe`.
2. Run the installer. Install location doesn't matter since this step only installs the actual setup script that it will run automatically.
3. Wait for the installer to finish. **Do not panic if it looks stuck.** It is just downloading dependencies.

## Installation (Android)

1. Navigate to Releases and download `app-release.apk`.
2. Allow third-party apk installs from your phone's settings.
3. Install the apk.

## Uninstallation

1. (PC) Either uninstall from the `Add or remove programs` Windows system settings or run the uninstaller located in the install location.
2. (Android) Uninstall through your phone's app settings.

## Build and install on PC from source

Needs Visual Studio 2022 (C++ workload) and CMake. OpenVR headers are downloaded automatically.

```powershell
cd host
cmake -B build -A x64
cmake --build build --config Release
.\install.ps1 -Package .\build\phonevr      # registers driver, downloads adb, configures SteamVR
```

`install.ps1` sets `steamvr.forcedDriver=phonevr` so SteamVR starts without a physical headset
(backup saved; `.\install.ps1 -Uninstall` restores it).

Needs a GPU with hardware encoding (NVENC or others). AMD/Intel cards untested yet. If you have any modern GPU, you already have this.

## Build the phone app from source

Open `android/` in Android Studio (or `gradle assembleDebug` with a Gradle 8.9+ install) and run it
on the phone. Needs Android 8.0+ and a gyroscope.

## Use

1. Start SteamVR on the PC.
2. USB: enable *Developer options -> USB debugging*, plug in, accept the prompt. WiFi: use 5 GHz, same network.
3. Open PhoneVR on the phone, press **Start**, put it in the holder.

## Tuning (`%LOCALAPPDATA%\PhoneVR\phonevr\resources\settings\default.vrsettings`)

| Key | Meaning |
|---|---|
| `streamWidth/Height` | Whole side-by-side frame. Set it to your phone's aspect (e.g. 2400x1080) |
| `refreshRate` | 60 default; use your phone's rate |
| `bitrateKbps` | Compression level (phone slider overrides). USB: 40-80 Mbps; WiFi: 15-35 Mbps |
| `renderScale` | SteamVR render size vs. eye size (1.4 default; lower = faster) |
| `verticalFovDegrees`, `lensK1`, `lensK2` | Distortion for your lenses (straight lines in the world should look straight) |
| `lensCenterShiftX/Y` | Move lens centre (fraction of eye width; + = toward nose) to match your IPD/holder |
| `chromaticAberration` | Small value like 0.01 if colour fringes appear |
| `photonLatencyMs` | How far ahead SteamVR predicts your head. Higher values compensate for delay but overshoot when you stop turning your head. |
| `useMainProfile` | H.264 Main profile instead of Baseline (better quality but more decoder buffering). |
| `gpuIndex` | Pick a GPU on multi-GPU systems (-1 = largest VRAM) |

Restart SteamVR after editing.

## Phone app settings

Open the app, adjust the options, then press **Start**. Every option is remembered for next time.

| Setting | Default | What it does |
|---|---|---|
| **Connection** | Auto | *Auto* tries USB first, then WiFi. *USB only* waits for the cable (needs USB debugging on the phone). *WiFi only* searches the local network for the PC. |
| **PC IP address** | empty | Optional. Leave it empty to find the PC automatically on WiFi. The app saves the address after the first successful WiFi connection, and falls back to automatic discovery if the PC's address changes. Type one in yourself if discovery is blocked (different subnet, firewall). |
| **Video bitrate** | PC default | Slider from 0 to 100 Mbps in 5 Mbps steps. `0` uses the `bitrateKbps` value in the PC settings. Lower means more compression and less bandwidth. Applied when the stream connects. Suggested: USB 40-80 Mbps, WiFi 15-35 Mbps. |
| **Flip landscape** | off | Tick if the picture is upside down in your holder, i.e. the phone's top edge points to your right instead of your left. |
| **Raw gyroscope mode** | off | Sends the unfiltered gyroscope and accelerometer readings, and the PC does the sensor fusion instead of Android. Results vary by phone: it made one test phone feel smoother but made another feel jittery, so try it both ways. The tilt stays level, but left-right drift builds slowly, as in the normal mode, so recenter now and then. |
| **Sustained performance mode** | on | Asks Android to cap the phone's clock speed to what it can hold indefinitely. Keeps the phone cooler. On some phones it adds occasional latency spikes; untick it to test. |
| **Frame pacing** | off | Experimental. Shows only the newest decoded frame once per display refresh. |
| **Show diagnostics overlay** | off | Draws latency numbers at the top-left of the screen (see below). |

### Controls while in VR

| Input | Action |
|---|---|
| Volume down, or double-tap the screen | Recenter (the direction you are facing becomes "forward") |
| Volume up | Cycle the screen brightness: 10% → 25% → 45% → 70% → 100% → back to 10%. The level is remembered and does not change your phone's normal brightness setting. |
| Back | Leave VR |

On the PC, `Ctrl+Alt+R` also recenters, and so does SteamVR's own reset.

### Diagnostics overlay

Turn it on, press Start, and read it with the phone in your hand (it is not readable inside a headset).

| Line | Meaning | Healthy |
|---|---|---|
| `sensor: N Hz, delivery avg / max` | How often the phone reports rotation, and how long a reading waits inside the phone before the app gets it | About 200 Hz, avg under 5 ms |
| `decode: avg / max` | Time a video frame spends in the phone's hardware decoder | Under about 15 ms at 2160x1080 |
| `fed->on screen: avg / max` | The wait for the display to show the frame. | Under about 10 ms |
| `link RTT: avg / max` | Round trip phone → PC → phone | A few ms; watch for spikes |
| `video queued in link: avg / max` | How much later each frame arrives than at its best moment | Near 0; a high number means the link is overloaded |

If `link RTT` or `video queued in link` stay high, lower the bitrate or switch to USB.
