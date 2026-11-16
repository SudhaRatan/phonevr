#include "../src/pose_math.h"
#include <cstdio>
using namespace phonevr;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static bool near(double a, double b, double e) { return std::fabs(a - b) < e; }
int main() {
    const double G = 9.80665;
    Quat q;
    uint64_t t = 1000000000ull;

    // (a) spin about the vertical axis (device lying flat) at 1 rad/s for 2 s -> 2 rad of yaw
    {
        ImuFusion f;
        CHECK(f.update({0,0,0}, {0,0,G}, t, &q), "init");
        for (int i = 0; i < 400; i++) { t += 5000000ull; f.update({0,0,1}, {0,0,G}, t, &q); }
        Vec3 x = qrotate(q, {1,0,0});
        CHECK(near(x.x, std::cos(2.0), 1e-3) && near(x.y, std::sin(2.0), 1e-3), "yaw integration: %f %f", x.x, x.y);
    }
    // (b) starts flat, then the accelerometer says it is tilted: estimate converges
    {
        ImuFusion f; t = 1000000000ull;
        f.update({0,0,0}, {0,0,G}, t, &q);
        Vec3 tilted = {0, G * 0.5, G * 0.8660254};
        for (int i = 0; i < 1600; i++) { t += 5000000ull; f.update({0,0,0}, tilted, t, &q); }
        Vec3 v = qrotate(qconj(q), {0,0,1});
        CHECK(near(v.y, 0.5, 0.02) && near(v.z, 0.8660254, 0.02), "tilt convergence: %f %f %f", v.x, v.y, v.z);
    }
    // (c) accelerometer magnitude far from 1 g (head being shaken): ignored
    {
        ImuFusion f; t = 1000000000ull;
        f.update({0,0,0}, {0,0,G}, t, &q);
        for (int i = 0; i < 400; i++) { t += 5000000ull; f.update({0,0,0}, {0, 7.5, 13.0}, t, &q); }
        Vec3 v = qrotate(qconj(q), {0,0,1});
        CHECK(near(v.z, 1.0, 1e-6), "gate: %f %f %f", v.x, v.y, v.z);
    }
    // (d) first sample tilted: initial orientation already matches gravity
    {
        ImuFusion f;
        Vec3 a = {G * 0.6, 0, G * 0.8};
        f.update({0,0,0}, a, 1000000000ull, &q);
        Vec3 v = qrotate(qconj(q), {0,0,1});
        CHECK(near(v.x, 0.6, 1e-6) && near(v.z, 0.8, 1e-6), "init tilt: %f %f %f", v.x, v.y, v.z);
        Vec3 up = qrotate(q, {0.6, 0, 0.8});
        CHECK(near(up.z, 1.0, 1e-6), "device up maps to world up: %f %f %f", up.x, up.y, up.z);
    }
    // (e) no usable accel yet -> no output
    { ImuFusion f; CHECK(!f.update({0,0,0}, {0,0,0}, t, &q), "needs accel"); }
    // (f) pitch: rotate about device x at 1 rad/s for 0.5 s starting flat; accel follows gravity
    {
        ImuFusion f; t = 1000000000ull;
        f.update({0,0,0}, {0,0,G}, t, &q);
        for (int i = 1; i <= 100; i++) {
            t += 5000000ull;
            double ang = i * 0.005;                       // device tilted by ang about x
            Vec3 a = {0, G * std::sin(ang), G * std::cos(ang)};  // "up" as seen by the device after rotating +ang about its x axis
            f.update({1,0,0}, a, t, &q);
        }
        Vec3 v = qrotate(qconj(q), {0,0,1});
        CHECK(near(v.y, std::sin(0.5), 0.01) && near(v.z, std::cos(0.5), 0.01), "pitch: %f %f %f", v.x, v.y, v.z);
    }
    if (!fails) printf("imu_fusion: all tests passed\n");
    return fails ? 1 : 0;
}
