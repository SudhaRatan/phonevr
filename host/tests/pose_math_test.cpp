#include "../src/pose_math.h"
#include <cstdio>
#include <cstdlib>
using namespace phonevr;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static bool near(double a, double b, double e = 1e-6) { return std::fabs(a - b) < e; }
static double angleBetween(const Quat& a, const Quat& b) {
    Quat d = qmul(qconj(a), b); double w = std::fabs(d.w); if (w > 1) w = 1; return 2 * std::acos(w);
}
const double PI = 3.14159265358979323846;
// device axes expressed in Android world (ENU) -> deviceToWorld quaternion
static Quat dev(Vec3 x, Vec3 y, Vec3 z) { return quatFromColumns(x, y, z); }
// rotate a deviceToWorld quaternion by yaw (CCW seen from above, about world up/z) and pitch (about world east)
static Quat worldYaw(double a) { return {std::cos(a/2), 0, 0, std::sin(a/2)}; }
static Quat worldPitchUp(double a) { return {std::cos(a/2), std::sin(a/2), 0, 0}; } // about east (x): +angle tilts north toward up
int main() {
    // Phone landscape, top to the user's left (ROTATION_90), user faces north, level.
    Quat d90 = dev({0,0,1}, {-1,0,0}, {0,-1,0});
    Quat h = headOrientation(d90, 1);
    CHECK(angleBetween(h, Quat{}) < 1e-6, "rot90 facing north should be identity, got %f %f %f %f", h.w,h.x,h.y,h.z);
    // Phone top to the user's right (ROTATION_270)
    Quat d270 = dev({0,0,-1}, {1,0,0}, {0,-1,0});
    h = headOrientation(d270, 3);
    CHECK(angleBetween(h, Quat{}) < 1e-6, "rot270 facing north should be identity");
    // Turn head left 90 deg (north -> west)
    Quat west = qmul(worldYaw(PI/2), d90);
    h = headOrientation(west, 1);
    CHECK(near(yawOf(h), PI/2), "left turn yaw = %f", yawOf(h));
    Vec3 f = qrotate(h, {0,0,-1});
    CHECK(near(f.x, -1) && near(f.z, 0), "forward = %f %f %f", f.x, f.y, f.z);
    // Look up 30 deg: forward gets +y in OpenVR
    Quat up = qmul(worldPitchUp(PI/6), d90);
    f = qrotate(headOrientation(up, 1), {0,0,-1});
    CHECK(near(f.y, std::sin(PI/6)), "pitch up forward.y = %f", f.y);
    // Head's up vector stays +y when yawing only
    Vec3 u = qrotate(headOrientation(west, 1), {0,1,0});
    CHECK(near(u.y, 1), "up stays up");
    // Roll check: tilt head right (clockwise looking forward) => user's up vector leans toward +x
    // (rotation about world north by +a leans the up vector toward east)
    Quat rollR = qmul(Quat{std::cos(0.1), 0, std::sin(0.1), 0}, d90); // +0.2 rad about north leans up toward east
    u = qrotate(headOrientation(rollR, 1), {0,1,0});
    CHECK(u.x > 0.1, "tilting head right moves up-vector to +x, got %f", u.x);

    // Tracker + recenter
    HeadTracker t;
    t.update(west, 1, 1000000000ull); // first sample auto-centers
    CHECK(near(yawOf(t.orientation()), 0), "auto-center yaw=%f", yawOf(t.orientation()));
    Quat southwest = qmul(worldYaw(PI/2 + PI/4), d90);
    t.update(southwest, 1, 1010000000ull);
    CHECK(near(yawOf(t.orientation()), PI/4), "after turning 45 more, yaw=%f", yawOf(t.orientation()));
    t.recenter();
    t.update(southwest, 1, 1020000000ull);
    CHECK(near(yawOf(t.orientation()), 0), "recenter yaw=%f", yawOf(t.orientation()));
    // recenter must not change pitch
    Quat pitched = qmul(worldYaw(1.0), qmul(worldPitchUp(0.4), d90));
    t.recenter(); t.update(pitched, 1, 1030000000ull);
    f = qrotate(t.orientation(), {0,0,-1});
    CHECK(near(f.x, 0) && near(f.y, std::sin(0.4)), "recenter keeps pitch f=%f %f %f", f.x, f.y, f.z);

    // Angular velocity: yaw left at 2 rad/s, 250 Hz samples
    HeadTracker v;
    double w = 2.0; uint64_t ts = 1000000000ull;
    for (int i = 0; i < 200; i++) {
        double a = w * (i * 0.004);
        v.update(qmul(worldYaw(a), d90), 1, ts + (uint64_t)(i * 4000000ull));
    }
    Vec3 av = v.angularVel();
    CHECK(near(av.y, 2.0, 0.05) && near(av.x, 0, 0.01) && near(av.z, 0, 0.01), "angvel = %f %f %f", av.x, av.y, av.z);
    if (!fails) printf("pose_math: all tests passed\n");
    return fails ? 1 : 0;
}
