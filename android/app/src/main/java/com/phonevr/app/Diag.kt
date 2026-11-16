package com.phonevr.app

import java.util.Locale

/** Collects values and publishes (average, max) in milliseconds once per second. Input is microseconds. */
class Stat(private val publish: (avgMs: Float, maxMs: Float) -> Unit) {
    private var start = 0L
    private var n = 0
    private var sum = 0L
    private var max = 0L

    fun add(valueUs: Long, nowUs: Long) {
        if (start == 0L) start = nowUs
        n++
        sum += valueUs
        if (valueUs > max) max = valueUs
        if (nowUs - start >= 1_000_000L) {
            publish((sum / n) / 1000f, max / 1000f)
            start = nowUs; n = 0; sum = 0L; max = 0L
        }
    }
}

/** Latency measurements shown by the optional diagnostics overlay. */
object Diag {
    @Volatile var sensorHz = 0
    @Volatile var sensorDelayAvgMs = 0f   // sensor event -> delivered to our code
    @Volatile var sensorDelayMaxMs = 0f
    @Volatile var decodeAvgMs = 0f        // video frame handed to decoder -> decoded
    @Volatile var decodeMaxMs = 0f
    @Volatile var renderAvgMs = 0f        // video frame handed to decoder -> drawn on screen
    @Volatile var renderMaxMs = 0f
    @Volatile var rttAvgMs = 0f           // phone -> PC -> phone round trip (waits behind video on the PC side)
    @Volatile var rttMaxMs = 0f
    @Volatile var queueAvgMs = 0f         // video arrival delay above its best case = queueing in the link
    @Volatile var queueMaxMs = 0f

    fun summary(): String = String.format(
        Locale.US,
        "sensor: %d Hz, delivery avg %.1f / max %.1f ms\n" +
        "decode: avg %.1f / max %.1f ms\n" +
        "fed->on screen: avg %.1f / max %.1f ms\n" +
        "link RTT: avg %.1f / max %.1f ms\n" +
        "video queued in link: avg %.1f / max %.1f ms",
        sensorHz, sensorDelayAvgMs, sensorDelayMaxMs,
        decodeAvgMs, decodeMaxMs, renderAvgMs, renderMaxMs,
        rttAvgMs, rttMaxMs, queueAvgMs, queueMaxMs
    )
}
