// Settings + logging shared by the driver sources.
#pragma once
#include <openvr_driver.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

namespace phonevr {

inline void Log(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    buf[n] = '\n';
    buf[n + 1] = 0;
    if (vr::VRDriverLog()) vr::VRDriverLog()->Log(buf);
}

struct Settings {
    uint32_t streamWidth = 2160;   // full side-by-side frame sent to the phone
    uint32_t streamHeight = 1080;
    float refreshRate = 60.f;
    uint32_t bitrateKbps = 30000;  // default; the phone app can override it
    float renderScale = 1.4f;      // SteamVR render target size = eye size * this
    float verticalFovDeg = 90.f;   // what SteamVR renders per eye
    float lensK1 = 0.25f;          // barrel pre-distortion: r' = r(1 + k1 r^2 + k2 r^4)
    float lensK2 = 0.08f;
    float chromatic = 0.f;         // per-channel radial scale difference (R=-c, B=+c)
    float lensCenterShiftX = 0.f;  // fraction of eye-viewport width, positive = toward nose
    float lensCenterShiftY = 0.f;  // fraction of eye-viewport height, positive = down
    float ipdMm = 63.f;
    float headHeightM = 1.6f;
    bool usbEnabled = true;
    std::string adbPath;           // empty = bundled adb, then PATH
    bool hotkeys = true;           // Ctrl+Alt+R recenters
    bool gazeController = true;    // virtual gaze-pointer controller (phone volume-up = click)
    int gpuIndex = -1;             // -1 = auto (largest VRAM)

    static Settings Load() {
        static const char* S = "driver_phonevr";
        auto* v = vr::VRSettings();
        Settings s;
        s.streamWidth = (uint32_t)v->GetInt32(S, "streamWidth");
        s.streamHeight = (uint32_t)v->GetInt32(S, "streamHeight");
        s.refreshRate = v->GetFloat(S, "refreshRate");
        s.bitrateKbps = (uint32_t)v->GetInt32(S, "bitrateKbps");
        s.renderScale = v->GetFloat(S, "renderScale");
        s.verticalFovDeg = v->GetFloat(S, "verticalFovDegrees");
        s.lensK1 = v->GetFloat(S, "lensK1");
        s.lensK2 = v->GetFloat(S, "lensK2");
        s.chromatic = v->GetFloat(S, "chromaticAberration");
        s.lensCenterShiftX = v->GetFloat(S, "lensCenterShiftX");
        s.lensCenterShiftY = v->GetFloat(S, "lensCenterShiftY");
        s.ipdMm = v->GetFloat(S, "ipdMm");
        s.headHeightM = v->GetFloat(S, "headHeightM");
        s.usbEnabled = v->GetBool(S, "usbEnabled");
        s.hotkeys = v->GetBool(S, "enableHotkeys");
        s.gazeController = !v->GetBool(S, "disableGazeController");
        s.gpuIndex = v->GetInt32(S, "gpuIndex");
        char buf[512] = {};
        v->GetString(S, "adbPath", buf, sizeof(buf));
        s.adbPath = buf;

        // sanitize (defaults above are used when the settings file is missing keys)
        Settings d;
        if (s.streamWidth < 640 || s.streamWidth > 7680) s.streamWidth = d.streamWidth;
        if (s.streamHeight < 360 || s.streamHeight > 4320) s.streamHeight = d.streamHeight;
        s.streamWidth &= ~3u;  // each eye must have an even width
        s.streamHeight &= ~1u;
        if (s.refreshRate < 24 || s.refreshRate > 240) s.refreshRate = d.refreshRate;
        if (s.bitrateKbps < 1000) s.bitrateKbps = d.bitrateKbps;
        s.renderScale = std::clamp(s.renderScale, 0.5f, 3.0f);
        s.verticalFovDeg = std::clamp(s.verticalFovDeg, 40.f, 150.f);
        s.ipdMm = std::clamp(s.ipdMm, 40.f, 80.f);
        return s;
    }
};

}  // namespace phonevr
