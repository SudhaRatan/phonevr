# PhoneVR: simple SteamVR phone headset

Simple PhoneVR implementation similar to already existing ones. Difference is, the PC does everything: SteamVR renders, the driver applies the lens distortion, converts to
NV12 on the GPU and hardware-encodes H.264. The phone only hardware-decodes into a plain
SurfaceView and streams its rotation sensor. **No Google Cardboard API, no OpenGL, no VR SDK**
on the phone, so there is almost no load or heat on it. I was unsatisfied with existing implementations cooking my phone so I made this instead. I might make executables if anyone is actually interested, otherwise just build from source using instructions below.

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

## Build and install (PC, Windows 10/11)

Needs Visual Studio 2022 (C++ workload) and CMake. OpenVR headers are downloaded automatically.

```powershell
cd host
cmake -B build -A x64
cmake --build build --config Release
.\install.ps1 -Package .\build\phonevr      # registers driver, downloads adb, configures SteamVR
```

`install.ps1` sets `steamvr.forcedDriver=phonevr` so SteamVR starts without a physical headset
(backup saved; `.\install.ps1 -Uninstall` restores it).

Needs a GPU with hardware encoding (NVENC or others). AMD cards untested yet. If you have any modern GPU, you already have this.

## Build the phone app

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
| `gpuIndex` | Pick a GPU on multi-GPU systems (-1 = largest VRAM) |

Restart SteamVR after editing.

