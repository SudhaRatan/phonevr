// The virtual headset: SteamVR device + display component + direct-mode component.
#pragma once
#include <atomic>
#include <mutex>
#include <thread>

#include "common.h"
#include "pose_math.h"
#include "protocol.h"
#include "video_pipeline.h"

namespace phonevr {

// A virtual right-hand controller whose pose follows the head (a gaze pointer).
// It has one input, the trigger, driven by the phone's volume-up key, so the
// SteamVR dashboard and menus can be clicked with only a phone.
class GazeController : public vr::ITrackedDeviceServerDriver {
public:
    vr::EVRInitError Activate(uint32_t objectId) override;
    void Deactivate() override { id_ = vr::k_unTrackedDeviceIndexInvalid; }
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char* resp, uint32_t len) override {
        if (len) resp[0] = 0;
    }
    vr::DriverPose_t GetPose() override;

    void UpdatePose(const vr::DriverPose_t& headPose);  // any thread
    void SetClick(bool down);                           // any thread

private:
    std::atomic<uint32_t> id_{vr::k_unTrackedDeviceIndexInvalid};
    vr::VRInputComponentHandle_t clickH_ = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t valueH_ = vr::k_ulInvalidInputComponentHandle;
};

class HmdDevice : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent {
public:
    HmdDevice(const Settings& s, VideoPipeline& pipeline);
    ~HmdDevice();

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t objectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char* name) override;
    void DebugRequest(const char*, char* resp, uint32_t len) override {
        if (len) resp[0] = 0;
    }
    vr::DriverPose_t GetPose() override;

    // IVRDisplayComponent
    void GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) override;
    bool IsDisplayOnDesktop() override { return false; }
    bool IsDisplayRealDisplay() override { return false; }
    void GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) override;
    void GetEyeOutputViewport(vr::EVREye eye, uint32_t* x, uint32_t* y, uint32_t* w,
                              uint32_t* h) override;
    void GetProjectionRaw(vr::EVREye eye, float* l, float* r, float* t, float* b) override;
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eye, float u, float v) override;
    bool ComputeInverseDistortion(vr::HmdVector2_t*, vr::EVREye, uint32_t, float, float) override {
        return false;
    }

    // phone events (network threads)
    void OnSensor(const SensorMsg& m);
    void Recenter();
    void OnClick(bool down) { if (gaze_) gaze_->SetClick(down); }
    void SetGazeController(GazeController* g) { gaze_ = g; }
    void OnPhoneConnected();
    void OnPhoneDisconnected();

    uint32_t objectId() const { return objectId_; }

private:
    class Direct;
    vr::DriverPose_t BuildPoseLocked() const;
    void PushPose(const vr::DriverPose_t& p);
    void VsyncLoop();

    Settings s_;
    VideoPipeline& pipe_;
    std::unique_ptr<Direct> direct_;
    GazeController* gaze_ = nullptr;

    uint32_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    mutable std::mutex poseMutex_;
    HeadTracker tracker_;
    std::atomic<bool> phoneConnected_{false};

    std::atomic<bool> running_{false};
    std::thread vsyncThread_;
};

}  // namespace phonevr
