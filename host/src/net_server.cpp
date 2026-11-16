#include "net_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <chrono>
#include <cstring>
#include <string>

namespace phonevr {

static const uintptr_t kInvalid = ~0ull;
static inline SOCKET S(uintptr_t s) { return (SOCKET)s; }

namespace {

// Buffered reader so 500 Hz sensor packets don't cost one syscall per field.
struct Reader {
    SOCKET s;
    uint8_t buf[65536];
    size_t pos = 0, len = 0;
    explicit Reader(SOCKET sock) : s(sock) {}
    bool readExact(void* dst, size_t n) {
        uint8_t* out = (uint8_t*)dst;
        while (n > 0) {
            if (pos == len) {
                int r = recv(s, (char*)buf, sizeof(buf), 0);
                if (r <= 0) return false;
                pos = 0;
                len = (size_t)r;
            }
            size_t take = std::min(n, len - pos);
            memcpy(out, buf + pos, take);
            pos += take;
            out += take;
            n -= take;
        }
        return true;
    }
    bool skip(size_t n) {
        uint8_t tmp[1024];
        while (n > 0) {
            size_t t = std::min(n, sizeof(tmp));
            if (!readExact(tmp, t)) return false;
            n -= t;
        }
        return true;
    }
};

std::string ModuleDir() {
    HMODULE h = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ModuleDir, &h);
    char path[MAX_PATH] = {};
    GetModuleFileNameA(h, path, MAX_PATH);
    std::string p = path;
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? "." : p.substr(0, slash);
}

bool FileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string ResolveAdb(const std::string& adbPathSetting) {
    if (!adbPathSetting.empty()) return adbPathSetting;
    std::string bundled = ModuleDir() + "\\adb\\adb.exe";
    return FileExists(bundled) ? bundled : "adb";
}

// Runs `adb <args>`. Returns the process exit code, or -1 if it could not run.
int RunAdb(const std::string& exe, const std::string& args, DWORD timeoutMs) {
    std::string cmd = "\"" + exe + "\" " + args;
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::vector<char> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    DWORD code = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi.hProcess, &code);
    } else {
        TerminateProcess(pi.hProcess, 1);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

int RunAdbReverse(const std::string& adbPathSetting) {
    std::string port = std::to_string(kTcpPort);
    return RunAdb(ResolveAdb(adbPathSetting), "reverse tcp:" + port + " tcp:" + port, 6000);
}

bool IsProcessRunning(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { found = true; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

}  // namespace

bool NetServer::Start() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return false;
    BOOL yes = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kTcpPort);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 2) != 0) {
        Log("PhoneVR: cannot listen on TCP %u (error %d)", kTcpPort, WSAGetLastError());
        closesocket(ls);
        return false;
    }
    listenSock_ = ls;

    SOCKET us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (us != INVALID_SOCKET) {
        setsockopt(us, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
        sockaddr_in ua = {};
        ua.sin_family = AF_INET;
        ua.sin_addr.s_addr = htonl(INADDR_ANY);
        ua.sin_port = htons(kDiscoveryPort);
        if (bind(us, (sockaddr*)&ua, sizeof(ua)) == 0) {
            udpSock_ = us;
        } else {
            Log("PhoneVR: cannot bind UDP %u; WiFi auto-discovery disabled", kDiscoveryPort);
            closesocket(us);
        }
    }

    running_ = true;
    acceptThread_ = std::thread(&NetServer::AcceptLoop, this);
    senderThread_ = std::thread(&NetServer::SenderLoop, this);
    if (udpSock_ != kInvalid) discoveryThread_ = std::thread(&NetServer::DiscoveryLoop, this);
    if (settings_.usbEnabled) {
        // If no adb server is running yet, the first `adb` call below starts one as a
        // child of vrserver. Steam keeps SteamVR "running" while any such child lives,
        // so we must stop it again on shutdown (only if we were the one to start it).
        weStartedAdb_ = !IsProcessRunning(L"adb.exe");
        adbThread_ = std::thread(&NetServer::AdbLoop, this);
    }
    Log("PhoneVR: listening on TCP %u (USB via adb reverse, WiFi via UDP discovery)", kTcpPort);
    return true;
}

void NetServer::Stop() {
    if (!running_.exchange(false)) return;
    if (listenSock_ != kInvalid) { closesocket(S(listenSock_)); listenSock_ = kInvalid; }
    if (udpSock_ != kInvalid) { closesocket(S(udpSock_)); udpSock_ = kInvalid; }
    {
        uintptr_t s = clientSock_.load();
        if (s != kInvalid) shutdown(S(s), SD_BOTH);
    }
    qCv_.notify_all();
    if (acceptThread_.joinable()) acceptThread_.join();
    if (discoveryThread_.joinable()) discoveryThread_.join();
    if (adbThread_.joinable()) adbThread_.join();
    if (weStartedAdb_) {
        weStartedAdb_ = false;
        RunAdb(ResolveAdb(settings_.adbPath), "kill-server", 4000);
    }
    if (senderThread_.joinable()) senderThread_.join();
    if (clientThread_.joinable()) clientThread_.join();
    WSACleanup();
}

void NetServer::DropClient() {
    uintptr_t s = clientSock_.load();
    if (s != kInvalid) shutdown(S(s), SD_BOTH);
    if (clientThread_.joinable()) clientThread_.join();
    std::lock_guard<std::mutex> lk(qMutex_);
    queue_.clear();
    waitingForIdr_ = true;
}

void NetServer::AcceptLoop() {
    while (running_) {
        sockaddr_in from = {};
        int fl = sizeof(from);
        SOCKET c = accept(S(listenSock_), (sockaddr*)&from, &fl);
        if (c == INVALID_SOCKET) {
            if (!running_) break;
            Sleep(50);
            continue;
        }
        BOOL yes = TRUE;
        setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&yes, sizeof(yes));
        DWORD timeoutMs = 3000;  // phone sends sensors constantly; silence = dead link
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
        int sndbuf = 256 * 1024;  // keep kernel buffering (= latency) small
        setsockopt(c, SOL_SOCKET, SO_SNDBUF, (const char*)&sndbuf, sizeof(sndbuf));

        DropClient();  // newest connection wins
        char ip[64] = {};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        Log("PhoneVR: phone connected from %s", ip);
        clientSock_ = (uintptr_t)c;
        clientThread_ = std::thread(&NetServer::ClientLoop, this, (uintptr_t)c);
    }
}

void NetServer::DiscoveryLoop() {
    while (running_) {
        char buf[128];
        sockaddr_in from = {};
        int fl = sizeof(from);
        int n = recvfrom(S(udpSock_), buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if (n <= 0) {
            if (!running_) break;
            Sleep(20);
            continue;
        }
        size_t reqLen = strlen(kDiscoveryRequest);
        if ((size_t)n >= reqLen && memcmp(buf, kDiscoveryRequest, reqLen) == 0) {
            sendto(S(udpSock_), kDiscoveryReply, (int)strlen(kDiscoveryReply), 0,
                   (sockaddr*)&from, fl);
        }
    }
}

void NetServer::AdbLoop() {
    int lastCode = 12345;
    while (running_) {
        if (!connected_) {
            int code = RunAdbReverse(settings_.adbPath);
            if (code != lastCode) {
                if (code == 0) Log("PhoneVR: USB link ready (adb reverse ok)");
                else if (code == -1) Log("PhoneVR: adb not found; USB mode needs adb (see README). WiFi still works");
                else Log("PhoneVR: no USB device authorized for adb (exit %d)", code);
                lastCode = code;
            }
        }
        for (int i = 0; i < 25 && running_; i++) Sleep(100);
    }
}

bool NetServer::SendAll(uintptr_t sock, const void* data, size_t len) {
    std::lock_guard<std::mutex> lk(sendMutex_);
    if (clientSock_.load() != sock) return false;
    const char* p = (const char*)data;
    while (len > 0) {
        int n = send(S(sock), p, (int)std::min<size_t>(len, 1 << 20), 0);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

void NetServer::ClientLoop(uintptr_t sock) {
    Reader rd(S(sock));
    bool gotHello = false;
    std::vector<uint8_t> scratch;
    while (running_) {
        MsgHeader h;
        if (!rd.readExact(&h, sizeof(h))) break;
        if (h.length > (1u << 20)) break;  // protocol violation
        switch (h.type) {
            case MSG_HELLO: {
                HelloMsg hello;
                if (h.length < sizeof(hello) || !rd.readExact(&hello, sizeof(hello))) goto done;
                if (!rd.skip(h.length - sizeof(hello))) goto done;
                if (hello.version != kProtocolVersion) {
                    Log("PhoneVR: protocol version mismatch (phone %u, host %u)", hello.version,
                        kProtocolVersion);
                    goto done;
                }
                uint32_t applied = cb_.onHello ? cb_.onHello(hello) : settings_.bitrateKbps;
                HelloAckMsg ack = {kProtocolVersion, settings_.streamWidth, settings_.streamHeight,
                                   settings_.refreshRate, applied, 0};
                MsgHeader ah = {MSG_HELLO_ACK, 0, 0, sizeof(ack)};
                uint8_t out[sizeof(ah) + sizeof(ack)];
                memcpy(out, &ah, sizeof(ah));
                memcpy(out + sizeof(ah), &ack, sizeof(ack));
                if (!SendAll(sock, out, sizeof(out))) goto done;
                gotHello = true;
                connected_ = true;
                Log("PhoneVR: phone %ux%u @%.0fHz, streaming %ux%u @%.0ffps %u kbps", hello.screenWidth,
                    hello.screenHeight, hello.refreshHz, ack.streamWidth, ack.streamHeight, ack.fps,
                    applied);
                break;
            }
            case MSG_SENSOR: {
                SensorMsg m;
                if (h.length < sizeof(m) || !rd.readExact(&m, sizeof(m))) goto done;
                if (!rd.skip(h.length - sizeof(m))) goto done;
                if (gotHello && cb_.onSensor) cb_.onSensor(m);
                break;
            }
            case MSG_SET_BITRATE: {
                SetBitrateMsg m;
                if (h.length < sizeof(m) || !rd.readExact(&m, sizeof(m))) goto done;
                if (!rd.skip(h.length - sizeof(m))) goto done;
                if (cb_.onSetBitrate) cb_.onSetBitrate(m.bitrateKbps);
                break;
            }
            case MSG_CLICK: {
                uint8_t pressed = 0;
                if (h.length < 1 || !rd.readExact(&pressed, 1)) goto done;
                if (!rd.skip(h.length - 1)) goto done;
                if (gotHello && cb_.onClick) cb_.onClick(pressed != 0);
                break;
            }
            case MSG_RECENTER:
                if (!rd.skip(h.length)) goto done;
                if (cb_.onRecenter) cb_.onRecenter();
                break;
            case MSG_REQUEST_IDR:
                if (!rd.skip(h.length)) goto done;
                if (cb_.onRequestIdr) cb_.onRequestIdr();
                break;
            default:
                if (!rd.skip(h.length)) goto done;
                break;
        }
    }
done:
    {
        std::lock_guard<std::mutex> lk(sendMutex_);
        if (clientSock_.load() == sock) clientSock_ = kInvalid;
        closesocket(S(sock));
    }
    if (connected_.exchange(false)) {
        Log("PhoneVR: phone disconnected");
        if (cb_.onDisconnect) cb_.onDisconnect();
    }
}

void NetServer::SendVideo(const uint8_t* data, size_t size, bool keyframe, uint64_t frameId,
                          uint64_t hostTimeUs) {
    if (!connected_) return;
    bool needIdr = false;
    {
        std::lock_guard<std::mutex> lk(qMutex_);
        if (waitingForIdr_ && !keyframe) return;  // decoder can't use it
        if (queue_.size() >= 3) {
            // Link is not keeping up: throw away the backlog instead of adding
            // latency, and resynchronize on the next keyframe.
            queue_.clear();
            waitingForIdr_ = true;
            needIdr = true;
        }
        if (!needIdr || keyframe) {
            if (keyframe) waitingForIdr_ = false;
            Packet p;
            p.bytes.resize(sizeof(MsgHeader) + sizeof(VideoMsg) + size);
            MsgHeader h = {MSG_VIDEO, 0, 0, (uint32_t)(sizeof(VideoMsg) + size)};
            VideoMsg v = {};
            v.frameId = frameId;
            v.hostTimeUs = hostTimeUs;
            v.keyframe = keyframe ? 1 : 0;
            memcpy(p.bytes.data(), &h, sizeof(h));
            memcpy(p.bytes.data() + sizeof(h), &v, sizeof(v));
            memcpy(p.bytes.data() + sizeof(h) + sizeof(v), data, size);
            queue_.push_back(std::move(p));
            needIdr = needIdr && !keyframe;
        }
    }
    qCv_.notify_one();
    if (needIdr && cb_.onRequestIdr) cb_.onRequestIdr();
}

void NetServer::SenderLoop() {
    while (running_) {
        Packet p;
        {
            std::unique_lock<std::mutex> lk(qMutex_);
            qCv_.wait(lk, [&] { return !running_ || !queue_.empty(); });
            if (!running_) break;
            p = std::move(queue_.front());
            queue_.pop_front();
        }
        uintptr_t sock = clientSock_.load();
        if (sock == kInvalid) continue;
        if (!SendAll(sock, p.bytes.data(), p.bytes.size())) {
            shutdown(S(sock), SD_BOTH);  // reader thread notices and cleans up
        }
    }
}

}  // namespace phonevr
