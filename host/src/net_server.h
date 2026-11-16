// TCP server (video + sensors), UDP discovery responder, and the `adb reverse`
// helper that makes USB tethering work without any user setup.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "common.h"
#include "protocol.h"

namespace phonevr {

class NetServer {
public:
    struct Callbacks {
        // Returns the bitrate (kbps) that was actually applied.
        std::function<uint32_t(const HelloMsg&)> onHello;
        std::function<void()> onDisconnect;
        std::function<void(const SensorMsg&)> onSensor;
        std::function<void()> onRecenter;
        std::function<void()> onRequestIdr;
        std::function<void(uint32_t)> onSetBitrate;
        std::function<void(bool)> onClick;
        std::function<void(const ImuMsg&)> onImu;
    };

    NetServer(const Settings& s, Callbacks cb) : settings_(s), cb_(std::move(cb)) {}
    ~NetServer() { Stop(); }

    bool Start();
    void Stop();
    bool Connected() const { return connected_.load(); }

    // Called by the encoder thread. Never blocks on the network; if the
    // network cannot keep up it drops frames and asks for a fresh keyframe.
    void SendVideo(const uint8_t* data, size_t size, bool keyframe, uint64_t frameId,
                   uint64_t hostTimeUs);

private:
    struct Packet {
        std::vector<uint8_t> bytes;  // fully framed message
    };

    void AcceptLoop();
    void DiscoveryLoop();
    void AdbLoop();
    void SenderLoop();
    void ClientLoop(uintptr_t sock);
    bool SendAll(uintptr_t sock, const void* data, size_t len);
    void DropClient();

    Settings settings_;
    Callbacks cb_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    bool weStartedAdb_ = false;   // we spawned the adb server, so we must stop it
    uintptr_t listenSock_ = ~0ull;
    uintptr_t udpSock_ = ~0ull;

    std::mutex clientMutex_;      // guards clientSock_ / clientThread_
    std::atomic<uintptr_t> clientSock_{~0ull};
    std::thread clientThread_;
    std::mutex sendMutex_;        // serializes writes to the client socket

    std::mutex qMutex_;
    std::condition_variable qCv_;
    std::deque<Packet> queue_;
    bool waitingForIdr_ = true;

    std::thread acceptThread_, discoveryThread_, adbThread_, senderThread_;
};

}  // namespace phonevr
