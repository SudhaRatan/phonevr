// Pure math (no Windows / OpenVR dependencies, so it can be unit-tested anywhere).
//
// Responsibilities:
//   * convert the phone's game-rotation-vector quaternion into an OpenVR head
//     orientation (right-handed, +x right, +y up, -z forward)
//   * handle the phone sitting in a landscape holder (screen facing the eyes)
//   * yaw recentering
//   * angular velocity estimation (so SteamVR can predict ahead)
#pragma once
#include <cmath>
#include <atomic>
#include <cstdint>

namespace phonevr {

struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
};
struct Vec3 {
    double x = 0, y = 0, z = 0;
};

inline Quat qmul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat qconj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
inline Quat qnormalize(Quat q) {
    double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (n < 1e-12) return {1, 0, 0, 0};
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}
inline Vec3 qrotate(const Quat& q, const Vec3& v) {
    Quat p{0, v.x, v.y, v.z};
    Quat r = qmul(qmul(q, p), qconj(q));
    return {r.x, r.y, r.z};
}

// Column-major 3x3 -> quaternion (columns are the images of the basis vectors).
inline Quat quatFromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2) {
    double m00 = c0.x, m10 = c0.y, m20 = c0.z;
    double m01 = c1.x, m11 = c1.y, m21 = c1.z;
    double m02 = c2.x, m12 = c2.y, m22 = c2.z;
    Quat q;
    double tr = m00 + m11 + m22;
    if (tr > 0) {
        double s = std::sqrt(tr + 1.0) * 2;
        q.w = 0.25 * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        double s = std::sqrt(1.0 + m00 - m11 - m22) * 2;
        q.w = (m21 - m12) / s;
        q.x = 0.25 * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        double s = std::sqrt(1.0 + m11 - m00 - m22) * 2;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25 * s;
        q.z = (m12 + m21) / s;
    } else {
        double s = std::sqrt(1.0 + m22 - m00 - m11) * 2;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25 * s;
    }
    return qnormalize(q);
}

// Android world frame (game rotation vector): x = east-ish, y = north-ish, z = up.
// OpenVR: x = right, y = up, z = back.   v_vr = C * v_android_world
//   x_vr =  x_aw ; y_vr = z_aw ; z_vr = -y_aw
inline Quat androidWorldToOpenVR() {
    return quatFromColumns(/*image of android x*/ {1, 0, 0},
                           /*image of android y*/ {0, 0, -1},
                           /*image of android z*/ {0, 1, 0});
}

// The phone is held in landscape with the screen facing the user's eyes.
// Device axes: +x = right of portrait screen, +y = top of portrait screen,
// +z = out of the screen toward the user's eyes.
// Head axes (OpenVR): right, up, back (=toward the face, i.e. device +z).
//
// displayRotation (Surface.ROTATION_*):
//   1 (ROTATION_90):  the phone's top points to the user's LEFT.
//        user right = device -y, user up = device +x
//   3 (ROTATION_270): the phone's top points to the user's RIGHT.
//        user right = device +y, user up = device -x
//   0 / 2 (portrait) are handled too, for completeness.
inline Quat headFromDevice(int displayRotation) {
    Vec3 right, up, back{0, 0, 1};
    switch (displayRotation & 3) {
        case 1: right = {0, -1, 0}; up = {1, 0, 0}; break;
        case 3: right = {0, 1, 0};  up = {-1, 0, 0}; break;
        case 2: right = {-1, 0, 0}; up = {0, -1, 0}; break;
        default: right = {1, 0, 0}; up = {0, 1, 0}; break;
    }
    // Quaternion that maps head-local axes into device coordinates.
    return quatFromColumns(right, up, back);
}

// Head orientation in OpenVR space from the phone's rotation-vector quaternion.
inline Quat headOrientation(const Quat& deviceToWorld, int displayRotation) {
    return qnormalize(
        qmul(qmul(androidWorldToOpenVR(), deviceToWorld), headFromDevice(displayRotation)));
}

// Yaw (rotation about +y) of the head's forward vector.
inline double yawOf(const Quat& q) {
    Vec3 f = qrotate(q, {0, 0, -1});
    return std::atan2(-f.x, -f.z);
}

inline Quat yawQuat(double yaw) {
    return {std::cos(yaw / 2), 0, std::sin(yaw / 2), 0};
}

// World-frame angular velocity from two orientations dt seconds apart.
inline Vec3 angularVelocity(const Quat& prev, const Quat& cur, double dt) {
    if (dt <= 1e-6) return {};
    Quat d = qmul(cur, qconj(prev));
    if (d.w < 0) { d.w = -d.w; d.x = -d.x; d.y = -d.y; d.z = -d.z; }
    double s = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (s < 1e-9) return {};
    double angle = 2.0 * std::atan2(s, d.w);
    double k = angle / s / dt;
    return {d.x * k, d.y * k, d.z * k};
}

// Stateful tracker: feed raw samples, read back recentered pose + velocity.
class HeadTracker {
public:
    void update(const Quat& deviceToWorld, int displayRotation, uint64_t timestampNs) {
        Quat raw = headOrientation(deviceToWorld, displayRotation);
        rawLatest_ = raw;
        if (!haveSample_) {
            haveSample_ = true;
            recenterPending_ = true;  // auto-center on first sample
        }
        if (recenterPending_) {
            yawOffset_ = yawOf(raw);
            recenterPending_ = false;
        }
        Quat out = qnormalize(qmul(yawQuat(-yawOffset_), raw));

        // Angular velocity: use >= 4 ms baselines to keep noise down, low-pass it.
        if (haveVel_) {
            double dt = (timestampNs - velRefTimeNs_) * 1e-9;
            if (dt >= 0.004) {
                Vec3 w = angularVelocity(velRef_, out, dt);
                const double a = 0.5;
                vel_ = {vel_.x + a * (w.x - vel_.x), vel_.y + a * (w.y - vel_.y),
                        vel_.z + a * (w.z - vel_.z)};
                velRef_ = out;
                velRefTimeNs_ = timestampNs;
            }
        } else {
            velRef_ = out;
            velRefTimeNs_ = timestampNs;
            haveVel_ = true;
        }
        orientation_ = out;
    }

    // Next sample will define "forward". Safe to call from any sample thread.
    void recenter() { recenterPending_ = true; }

    Quat orientation() const { return orientation_; }
    Vec3 angularVel() const { return vel_; }
    bool valid() const { return haveSample_; }

    void reset() {
        rawLatest_ = orientation_ = velRef_ = Quat();
        vel_ = Vec3();
        velRefTimeNs_ = 0;
        yawOffset_ = 0;
        haveSample_ = haveVel_ = false;
        recenterPending_ = false;
    }

private:
    Quat rawLatest_, orientation_, velRef_;
    Vec3 vel_;
    uint64_t velRefTimeNs_ = 0;
    double yawOffset_ = 0;
    bool haveSample_ = false, haveVel_ = false;
    std::atomic<bool> recenterPending_{false};
};

// Gyro + accelerometer fusion (Mahony-style complementary filter), run on the PC.
//  * orientation comes from integrating the gyroscope at its full rate
//  * the accelerometer only slowly pulls pitch/roll toward gravity
//  * yaw has no absolute reference (no magnetometer) and drifts: use recenter
// Output quaternion maps device axes -> world axes, with world +z = up, the same
// convention as Android's GAME_ROTATION_VECTOR, so it feeds HeadTracker directly.
class ImuFusion {
public:
    void reset() { *this = ImuFusion(); }

    // Returns false until the first usable accelerometer sample has arrived.
    bool update(const Vec3& gyro, const Vec3& accel, uint64_t tsNs, Quat* out) {
        const double an = norm(accel);
        const bool accelOk = an > 4.0 && an < 20.0;
        if (!init_) {
            if (!accelOk) return false;
            q_ = tiltFromUp({accel.x / an, accel.y / an, accel.z / an});
            lastNs_ = tsNs;
            init_ = true;
            *out = q_;
            return true;
        }
        if (tsNs <= lastNs_) { *out = q_; return true; }
        double dt = (double)(tsNs - lastNs_) * 1e-9;
        lastNs_ = tsNs;
        if (dt > 0.05) dt = 0.05;  // after a stall, do not integrate a huge step

        Vec3 w = gyro;
        // Only trust the accelerometer when the head is not accelerating much.
        if (accelOk && std::fabs(an - 9.80665) < 2.5) {
            Vec3 a = {accel.x / an, accel.y / an, accel.z / an};
            Vec3 v = qrotate(qconj(q_), {0, 0, 1});  // estimated "up" in device axes
            Vec3 e = cross(a, v);
            w = {w.x + kp_ * e.x, w.y + kp_ * e.y, w.z + kp_ * e.z};
        }
        const double wn = norm(w);
        const double ang = wn * dt;
        Quat dq;
        if (ang < 1e-9) {
            dq = {1, 0.5 * w.x * dt, 0.5 * w.y * dt, 0.5 * w.z * dt};
        } else {
            const double s = std::sin(ang / 2) / wn;
            dq = {std::cos(ang / 2), w.x * s, w.y * s, w.z * s};
        }
        q_ = qnormalize(qmul(q_, dq));
        *out = q_;
        return true;
    }

    void setTiltGain(double kp) { kp_ = kp; }

private:
    static double norm(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    // Shortest rotation taking the device-frame vector u onto world up (0,0,1).
    static Quat tiltFromUp(const Vec3& u) {
        const double d = u.z;  // u . (0,0,1)
        if (d < -0.999999) return {0, 1, 0, 0};
        return qnormalize({1.0 + d, u.y * 1.0 - 0.0, -u.x, 0.0});  // (1+d, u x (0,0,1))
    }

    Quat q_;
    uint64_t lastNs_ = 0;
    bool init_ = false;
    double kp_ = 1.0;  // 1/s: tilt error time constant of about one second
};

}  // namespace phonevr
