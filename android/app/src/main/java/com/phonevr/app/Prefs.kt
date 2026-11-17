package com.phonevr.app

import android.content.Context

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("phonevr", Context.MODE_PRIVATE)

    /** 0 = auto (USB first, then WiFi), 1 = USB only, 2 = WiFi only */
    var mode: Int
        get() = sp.getInt("mode", StreamClient.MODE_AUTO)
        set(v) = sp.edit().putInt("mode", v).apply()

    var host: String
        get() = sp.getString("host", "") ?: ""
        set(v) = sp.edit().putString("host", v).apply()

    /** 0 = use the PC's configured bitrate */
    var bitrateKbps: Int
        get() = sp.getInt("bitrateKbps", 0)
        set(v) = sp.edit().putInt("bitrateKbps", v).apply()

    /** Android's sustained-performance mode caps clocks to what the phone can hold (cooler, but can add latency). */
    var sustainedPerf: Boolean
        get() = sp.getBoolean("sustainedPerf", true)
        set(v) = sp.edit().putBoolean("sustainedPerf", v).apply()

    /** Show only the newest decoded frame once per display refresh (prevents a display-queue backlog). */
    var framePacing: Boolean
        get() = sp.getBoolean("framePacing", false)
        set(v) = sp.edit().putBoolean("framePacing", v).apply()

    /** Index into the brightness steps in VrActivity (4 = full brightness). */
    var brightnessStep: Int
        get() = sp.getInt("brightnessStep", 4)
        set(v) = sp.edit().putInt("brightnessStep", v).apply()

    var showDiag: Boolean
        get() = sp.getBoolean("showDiag", false)
        set(v) = sp.edit().putBoolean("showDiag", v).apply()

    /** true = send raw gyro + accelerometer; the PC does the sensor fusion (lower latency on weak phones) */
    var rawGyro: Boolean
        get() = sp.getBoolean("rawGyro", false)
        set(v) = sp.edit().putBoolean("rawGyro", v).apply()

    /** true = phone's top edge points to the user's right */
    var reverseLandscape: Boolean
        get() = sp.getBoolean("reverseLandscape", false)
        set(v) = sp.edit().putBoolean("reverseLandscape", v).apply()
}
