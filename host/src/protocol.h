// PhoneVR wire protocol (shared by the SteamVR driver and the Android app).
//
// Transport: one TCP connection (TCP_NODELAY on both ends).
//   - USB:  the phone connects to 127.0.0.1:kTcpPort, which `adb reverse`
//           forwards to the PC. The driver sets this up automatically.
//   - WiFi: the phone finds the PC with a UDP broadcast on kDiscoveryPort,
//           then connects to the PC's address on kTcpPort.
//
// Framing: every message is  [u8 type][u8 reserved][u16 reserved][u32 length]
// followed by `length` payload bytes. All integers are little-endian.
#pragma once
#include <cstdint>

namespace phonevr {

constexpr uint16_t kTcpPort = 9943;
constexpr uint16_t kDiscoveryPort = 9944;
constexpr uint32_t kProtocolVersion = 1;

constexpr char kDiscoveryRequest[] = "PHONEVR_DISCOVER";
// Reply: "PHONEVR_HOST" (12 bytes)  -- TCP port is always kTcpPort.
constexpr char kDiscoveryReply[] = "PHONEVR_HOST";

enum MsgType : uint8_t {
    // phone -> PC
    MSG_HELLO = 1,
    MSG_SENSOR = 2,
    MSG_RECENTER = 3,
    MSG_SET_BITRATE = 4,
    MSG_REQUEST_IDR = 5,
    MSG_CLICK = 6,           // payload: first byte 1 = pressed, 0 = released
    // PC -> phone
    MSG_HELLO_ACK = 101,
    MSG_VIDEO = 102,
};

#pragma pack(push, 1)
struct MsgHeader {
    uint8_t type;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t length;
};

struct HelloMsg {            // phone -> PC
    uint32_t version;
    uint32_t screenWidth;    // phone's landscape pixel size
    uint32_t screenHeight;
    float refreshHz;
    uint32_t bitrateKbps;    // 0 = use the host's configured bitrate
};

struct HelloAckMsg {         // PC -> phone
    uint32_t version;
    uint32_t streamWidth;
    uint32_t streamHeight;
    float fps;
    uint32_t bitrateKbps;
    uint32_t codec;          // 0 = H.264 Annex-B
};

struct SensorMsg {           // phone -> PC (sent at sensor rate, ~200-500 Hz)
    uint64_t timestampNs;    // SensorEvent.timestamp (phone clock)
    float qx, qy, qz, qw;    // game-rotation-vector, device -> world (ENU-ish)
    uint8_t displayRotation; // Surface.ROTATION_0..3 of the VR activity
    uint8_t pad[3];
};

struct SetBitrateMsg {       // phone -> PC
    uint32_t bitrateKbps;
};

struct VideoMsg {            // PC -> phone; followed by the H.264 access unit
    uint64_t frameId;
    uint64_t hostTimeUs;     // host steady clock when encoded (for debugging)
    uint8_t keyframe;
    uint8_t pad[7];
};
#pragma pack(pop)

static_assert(sizeof(MsgHeader) == 8, "header size");
static_assert(sizeof(HelloMsg) == 20, "hello size");
static_assert(sizeof(HelloAckMsg) == 24, "ack size");
static_assert(sizeof(SensorMsg) == 28, "sensor size");
static_assert(sizeof(VideoMsg) == 24, "video size");

}  // namespace phonevr
