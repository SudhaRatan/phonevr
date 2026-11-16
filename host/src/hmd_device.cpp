#include "hmd_device.h"

#include <windows.h>
#include <timeapi.h>

#include <chrono>
#include <cmath>
#include <cstring>

namespace phonevr {

// ---------------------------------------------------------------------------
// Direct mode: SteamVR renders straight into textures we own.
// ---------------------------------------------------------------------------
class HmdDevice::Direct : public vr::IVRDriverDirectModeComponent {
public:
    explicit Direct(VideoPipeline& p) : pipe_(p) {}

    void CreateSwapTextureSet(uint32_t pid, const SwapTextureSetDesc_t* d,
                              SwapTextureSet_t* out) override {
        uint64_t h[3] = {};
        pipe_.CreateSwapTextureSet(pid, d->nWidth, d->nHeight, d->nFormat, d->nSampleCount, h);
        for (int i = 0; i < 3; i++) out->rSharedTextureHandles[i] = (vr::SharedTextureHandle_t)h[i];
        out->unTextureFlags = 0;  // legacy (non-NT) shared handles
    }
    void DestroySwapTextureSet(vr::SharedTextureHandle_t h) override { pipe_.DestroySwapTextureSet(h); }
    void DestroyAllSwapTextureSets(uint32_t pid) override { pipe_.DestroyAllSwapTextureSets(pid); }
    void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t handles[2], uint32_t (*idx)[2]) override {
        uint64_t h[2] = {handles[0], handles[1]};
        pipe_.NextSwapTextureSetIndex(h, *idx);
    }
    void SubmitLayer(const SubmitLayerPerEye_t (&eye)[2]) override {
        uint64_t h[2] = {eye[0].hTexture, eye[1].hTexture};
        float b[2][4];
        for (int i = 0; i < 2; i++) {
            b[i][0] = eye[i].bounds.uMin;
            b[i][1] = eye[i].bounds.vMin;
            b[i][2] = eye[i].bounds.uMax;
            b[i][3] = eye[i].bounds.vMax;
        }
        pipe_.SubmitLayer(h, b);
    }
    void Present(vr::SharedTextureHandle_t sync) override { pipe_.Present(sync); }
    void PostPresent(const Throttling_t*) override {}

private:
    VideoPipeline& pipe_;
};

// ---------------------------------------------------------------------------
// Gaze-pointer controller
// ---------------------------------------------------------------------------
vr::EVRInitError GazeController::Activate(uint32_t id) {
    id_ = id;
    auto* props = vr::VRProperties();
    vr::PropertyContainerHandle_t c = props->TrackedDeviceToPropertyContainer(id);
    props->SetStringProperty(c, vr::Prop_ModelNumber_String, "PhoneVR Gaze Pointer");
    props->SetStringProperty(c, vr::Prop_ManufacturerName_String, "PhoneVR");
    props->SetStringProperty(c, vr::Prop_TrackingSystemName_String, "phonevr");
    props->SetStringProperty(c, vr::Prop_ControllerType_String, "phonevr_gaze");
    props->SetInt32Property(c, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_RightHand);
    props->SetStringProperty(c, vr::Prop_InputProfilePath_String,
                             "{phonevr}/input/phonevr_gaze_profile.json");
    vr::VRDriverInput()->CreateScalarComponent(c, "/input/trigger/value", &valueH_,
                                               vr::VRScalarType_Absolute,
                                               vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(c, "/input/trigger/click", &clickH_);
    return vr::VRInitError_None;
}

vr::DriverPose_t GazeController::GetPose() {
    vr::DriverPose_t p = {};
    p.qWorldFromDriverRotation = {1, 0, 0, 0};
    p.qDriverFromHeadRotation = {1, 0, 0, 0};
    p.qRotation = {1, 0, 0, 0};
    return p;
}

void GazeController::UpdatePose(const vr::DriverPose_t& head) {
    uint32_t id = id_.load();
    if (id == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::DriverPose_t p = head;
    p.shouldApplyHeadModel = false;  // the pointer starts exactly at the head
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(id, p, sizeof(p));
}

void GazeController::SetClick(bool down) {
    if (id_.load() == vr::k_unTrackedDeviceIndexInvalid) return;
    vr::VRDriverInput()->UpdateBooleanComponent(clickH_, down, 0.0);
    vr::VRDriverInput()->UpdateScalarComponent(valueH_, down ? 1.f : 0.f, 0.0);
}

HmdDevice::HmdDevice(const Settings& s, VideoPipeline& pipeline)
    : s_(s), pipe_(pipeline), direct_(new Direct(pipeline)) {}

HmdDevice::~HmdDevice() { Deactivate(); }

vr::EVRInitError HmdDevice::Activate(uint32_t id) {
    objectId_ = id;
    auto* props = vr::VRProperties();
    vr::PropertyContainerHandle_t c = props->TrackedDeviceToPropertyContainer(id);

    props->SetStringProperty(c, vr::Prop_ModelNumber_String, "PhoneVR HMD");
    props->SetStringProperty(c, vr::Prop_ManufacturerName_String, "PhoneVR");
    props->SetStringProperty(c, vr::Prop_TrackingSystemName_String, "phonevr");
    props->SetStringProperty(c, vr::Prop_RenderModelName_String, "generic_hmd");
    props->SetFloatProperty(c, vr::Prop_UserIpdMeters_Float, s_.ipdMm * 0.001f);
    props->SetFloatProperty(c, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.f);
    props->SetFloatProperty(c, vr::Prop_DisplayFrequency_Float, s_.refreshRate);
    props->SetFloatProperty(c, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    props->SetUint64Property(c, vr::Prop_CurrentUniverseId_Uint64, 2);
    props->SetBoolProperty(c, vr::Prop_IsOnDesktop_Bool, false);
    props->SetBoolProperty(c, vr::Prop_DisplayDebugMode_Bool, false);
    props->SetBoolProperty(c, vr::Prop_HasDriverDirectModeComponent_Bool, true);
    props->SetBoolProperty(c, vr::Prop_DriverDirectModeSendsVsyncEvents_Bool, true);
    props->SetUint64Property(c, vr::Prop_GraphicsAdapterLuid_Uint64, pipe_.AdapterLuid());
    props->SetBoolProperty(c, vr::Prop_ContainsProximitySensor_Bool, false);
    props->SetBoolProperty(c, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);

    running_ = true;
    vsyncThread_ = std::thread(&HmdDevice::VsyncLoop, this);
    return vr::VRInitError_None;
}

void HmdDevice::Deactivate() {
    if (running_.exchange(false) && vsyncThread_.joinable()) vsyncThread_.join();
    objectId_ = vr::k_unTrackedDeviceIndexInvalid;
}

void* HmdDevice::GetComponent(const char* name) {
    if (!strcmp(name, vr::IVRDisplayComponent_Version))
        return static_cast<vr::IVRDisplayComponent*>(this);
    if (!strcmp(name, vr::IVRDriverDirectModeComponent_Version))
        return static_cast<vr::IVRDriverDirectModeComponent*>(direct_.get());
    return nullptr;
}

// ---------------------------------------------------------------------------
// Display component
// ---------------------------------------------------------------------------
void HmdDevice::GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) {
    *x = 0;
    *y = 0;
    *w = s_.streamWidth;
    *h = s_.streamHeight;
}

void HmdDevice::GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) {
    // Pre-distortion shrinks the image toward the lens centre, so rendering
    // somewhat above the eye viewport size keeps the centre sharp.
    *w = ((uint32_t)std::lround(s_.streamWidth * 0.5f * s_.renderScale)) & ~1u;
    *h = ((uint32_t)std::lround(s_.streamHeight * s_.renderScale)) & ~1u;
}

void HmdDevice::GetEyeOutputViewport(vr::EVREye eye, uint32_t* x, uint32_t* y, uint32_t* w,
                                     uint32_t* h) {
    *x = eye == vr::Eye_Left ? 0 : s_.streamWidth / 2;
    *y = 0;
    *w = s_.streamWidth / 2;
    *h = s_.streamHeight;
}

void HmdDevice::GetProjectionRaw(vr::EVREye, float* l, float* r, float* t, float* b) {
    const float aspect = (s_.streamWidth * 0.5f) / s_.streamHeight;
    const float ty = std::tan(s_.verticalFovDeg * 0.5f * 3.14159265f / 180.f);
    *l = -ty * aspect;
    *r = ty * aspect;
    *t = -ty;  // OpenVR convention: top is negative
    *b = ty;
}

vr::DistortionCoordinates_t HmdDevice::ComputeDistortion(vr::EVREye eye, float u, float v) {
    // Same math as the pixel shader so the compositor's view of the lens matches.
    const float aspect = (s_.streamWidth * 0.5f) / s_.streamHeight;
    const float sx = 2.f * aspect, sy = 2.f;
    const float cx = 0.5f + (eye == vr::Eye_Left ? s_.lensCenterShiftX : -s_.lensCenterShiftX);
    const float cy = 0.5f + s_.lensCenterShiftY;
    const float vx = (u - cx) * sx, vy = (v - cy) * sy;
    const float r2 = vx * vx + vy * vy;
    const float f = 1.f + s_.lensK1 * r2 + s_.lensK2 * r2 * r2;
    vr::DistortionCoordinates_t d;
    float* ch[3] = {d.rfRed, d.rfGreen, d.rfBlue};
    for (int i = 0; i < 3; i++) {
        float cf = f * (1.f + s_.chromatic * (i - 1));
        ch[i][0] = 0.5f + vx * cf / sx;
        ch[i][1] = 0.5f + vy * cf / sy;
    }
    return d;
}

// ---------------------------------------------------------------------------
// Pose
// ---------------------------------------------------------------------------
vr::DriverPose_t HmdDevice::BuildPoseLocked() const {
    vr::DriverPose_t p = {};
    const bool valid = phoneConnected_ && tracker_.valid();
    p.qWorldFromDriverRotation = {1, 0, 0, 0};
    p.qDriverFromHeadRotation = {1, 0, 0, 0};
    p.vecPosition[0] = 0;
    p.vecPosition[1] = s_.headHeightM;
    p.vecPosition[2] = 0;
    Quat q = tracker_.orientation();
    p.qRotation = {q.w, q.x, q.y, q.z};
    Vec3 w = tracker_.angularVel();
    p.vecAngularVelocity[0] = w.x;
    p.vecAngularVelocity[1] = w.y;
    p.vecAngularVelocity[2] = w.z;
    p.result = valid ? vr::TrackingResult_Running_OK : vr::TrackingResult_Uninitialized;
    p.poseIsValid = valid;
    p.deviceIsConnected = phoneConnected_;
    p.willDriftInYaw = true;          // no magnetometer: yaw drifts, user recenters
    p.shouldApplyHeadModel = true;    // SteamVR adds a neck model for a bit of parallax
    return p;
}

vr::DriverPose_t HmdDevice::GetPose() {
    std::lock_guard<std::mutex> lk(poseMutex_);
    return BuildPoseLocked();
}

void HmdDevice::PushPose(const vr::DriverPose_t& p) {
    if (objectId_ != vr::k_unTrackedDeviceIndexInvalid)
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(objectId_, p, sizeof(p));
    if (gaze_) gaze_->UpdatePose(p);
}

void HmdDevice::OnSensor(const SensorMsg& m) {
    vr::DriverPose_t pose;
    {
        std::lock_guard<std::mutex> lk(poseMutex_);
        tracker_.update(Quat{m.qw, m.qx, m.qy, m.qz}, m.displayRotation, m.timestampNs);
        pose = BuildPoseLocked();
    }
    PushPose(pose);
}

void HmdDevice::Recenter() {
    std::lock_guard<std::mutex> lk(poseMutex_);
    tracker_.recenter();  // applied on the next sensor sample
}

void HmdDevice::OnPhoneConnected() {
    std::lock_guard<std::mutex> lk(poseMutex_);
    tracker_.reset();  // first sample of the new session defines "forward"
    phoneConnected_ = true;
}

void HmdDevice::OnPhoneDisconnected() {
    vr::DriverPose_t pose;
    {
        std::lock_guard<std::mutex> lk(poseMutex_);
        phoneConnected_ = false;
        pose = BuildPoseLocked();
    }
    PushPose(pose);
    if (gaze_) gaze_->SetClick(false);  // never leave the trigger stuck down
}

// ---------------------------------------------------------------------------
// Vsync: the compositor in direct mode is paced by the driver.
// ---------------------------------------------------------------------------
void HmdDevice::VsyncLoop() {
    using clock = std::chrono::steady_clock;
    timeBeginPeriod(1);
    const auto interval = std::chrono::duration_cast<clock::duration>(
        std::chrono::duration<double>(1.0 / s_.refreshRate));
    auto next = clock::now() + interval;
    bool keyWasDown = false;
    int keyDivider = 0;
    while (running_) {
        auto now = clock::now();
        if (next - now > std::chrono::milliseconds(2))
            std::this_thread::sleep_for(next - now - std::chrono::milliseconds(1));
        while (clock::now() < next) {}  // short spin for accuracy
        vr::VRServerDriverHost()->VsyncEvent(0.0);
        next += interval;
        if (clock::now() > next + interval * 4) next = clock::now() + interval;  // fell behind

        if (s_.hotkeys && ++keyDivider >= 4) {  // ~15 Hz poll is plenty
            keyDivider = 0;
            bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000) &&
                        (GetAsyncKeyState('R') & 0x8000);
            if (down && !keyWasDown) {
                Log("PhoneVR: recenter (Ctrl+Alt+R)");
                Recenter();
            }
            keyWasDown = down;
        }
    }
    timeEndPeriod(1);
}

}  // namespace phonevr
