// SteamVR driver entry point: wires network <-> pipeline <-> virtual HMD.
#include <memory>

#include "common.h"
#include "hmd_device.h"
#include "net_server.h"
#include "video_pipeline.h"

using namespace phonevr;

namespace {

class PhoneVRProvider : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* ctx) override {
        VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
        Log("PhoneVR: driver starting");
        settings_ = Settings::Load();

        pipeline_ = std::make_unique<VideoPipeline>();
        if (!pipeline_->Init(settings_)) return vr::VRInitError_Driver_Failed;

        hmd_ = std::make_unique<HmdDevice>(settings_, *pipeline_);

        NetServer::Callbacks cb;
        cb.onHello = [this](const HelloMsg& h) {
            uint32_t kbps = h.bitrateKbps ? h.bitrateKbps : settings_.bitrateKbps;
            pipeline_->SetBitrate(kbps);
            pipeline_->RequestKeyframe();
            hmd_->OnPhoneConnected();
            return pipeline_->Bitrate();
        };
        cb.onDisconnect = [this] { hmd_->OnPhoneDisconnected(); };
        cb.onSensor = [this](const SensorMsg& m) { hmd_->OnSensor(m); };
        cb.onRecenter = [this] { hmd_->Recenter(); };
        cb.onRequestIdr = [this] { pipeline_->RequestKeyframe(); };
        cb.onSetBitrate = [this](uint32_t k) { pipeline_->SetBitrate(k); };
        cb.onClick = [this](bool down) { hmd_->OnClick(down); };
        net_ = std::make_unique<NetServer>(settings_, std::move(cb));

        pipeline_->SetPacketCallback([this](const uint8_t* d, size_t n, bool key, uint64_t id,
                                            uint64_t t) { net_->SendVideo(d, n, key, id, t); });

        if (!net_->Start()) return vr::VRInitError_Driver_Failed;

        vr::VRServerDriverHost()->TrackedDeviceAdded("PHONEVR-HMD-0001", vr::TrackedDeviceClass_HMD,
                                                     hmd_.get());
        if (settings_.gazeController) {
            gaze_ = std::make_unique<GazeController>();
            hmd_->SetGazeController(gaze_.get());
            vr::VRServerDriverHost()->TrackedDeviceAdded(
                "PHONEVR-GAZE-0001", vr::TrackedDeviceClass_Controller, gaze_.get());
        }
        return vr::VRInitError_None;
    }

    void Cleanup() override {
        if (net_) net_->Stop();
        net_.reset();
        hmd_.reset();
        gaze_.reset();
        if (pipeline_) pipeline_->Shutdown();
        pipeline_.reset();
        Log("PhoneVR: driver stopped");
    }

    const char* const* GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    void RunFrame() override {}
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    Settings settings_;
    std::unique_ptr<VideoPipeline> pipeline_;
    std::unique_ptr<HmdDevice> hmd_;
    std::unique_ptr<GazeController> gaze_;
    std::unique_ptr<NetServer> net_;
};

PhoneVRProvider g_provider;

}  // namespace

extern "C" __declspec(dllexport) void* HmdDriverFactory(const char* name, int* returnCode) {
    if (!strcmp(name, vr::IServerTrackedDeviceProvider_Version)) return &g_provider;
    if (returnCode) *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
