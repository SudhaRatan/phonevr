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

    /** true = phone's top edge points to the user's right */
    var reverseLandscape: Boolean
        get() = sp.getBoolean("reverseLandscape", false)
        set(v) = sp.edit().putBoolean("reverseLandscape", v).apply()
}
