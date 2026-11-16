package com.phonevr.app

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Process
import android.os.SystemClock

/**
 * Reports head rotation to the PC. Two modes:
 *
 *  - fused (default): Android's own GAME_ROTATION_VECTOR quaternion. Convenient, but on
 *    some (usually cheaper) phones it is computed in slow, heavily smoothed software,
 *    which feels like dragging a heavy object.
 *  - raw gyro: the unfiltered gyroscope at its full hardware rate plus the accelerometer.
 *    The PC does the sensor fusion, so the phone does almost nothing and nothing is smoothed.
 *
 * No Cardboard, no rendering. All head-pose maths happens on the PC.
 */
class SensorSender(
    context: Context,
    private val client: StreamClient,
    private val rawGyro: Boolean,
    private val displayRotation: () -> Int,
) : SensorEventListener {

    private val sm = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val thread = HandlerThread("phonevr-sensors", Process.THREAD_PRIORITY_URGENT_DISPLAY)
    private val q = FloatArray(4)
    private val accel = FloatArray(3)
    private var haveAccel = false
    private var lastNs = 0L
    private var lastRotCheckNs = 0L
    private var rotation = 0

    // latency statistics, published once per second
    private var winStartNs = 0L
    private var winCount = 0
    private var winSumNs = 0L
    private var winMaxNs = 0L

    fun start(): Boolean {
        if (rawGyro) {
            val gyro = sm.getDefaultSensor(Sensor.TYPE_GYROSCOPE) ?: return false
            val acc = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER) ?: return false
            thread.start()
            val h = Handler(thread.looper)
            val g = sm.registerListener(this, gyro, 2000 /* us = up to 500 Hz */, 0, h)
            val a = sm.registerListener(this, acc, 5000 /* 200 Hz */, 0, h)
            return g && a
        }
        // GAME_ROTATION_VECTOR = gyro + accelerometer only: no magnetometer jumps.
        // Yaw drift is handled by the recenter command.
        val sensor = sm.getDefaultSensor(Sensor.TYPE_GAME_ROTATION_VECTOR)
            ?: sm.getDefaultSensor(Sensor.TYPE_ROTATION_VECTOR)
            ?: return false
        thread.start()
        return sm.registerListener(this, sensor, 2000 /* us = up to 500 Hz */, 0, Handler(thread.looper))
    }

    fun stop() {
        sm.unregisterListener(this)
        thread.quitSafely()
    }

    /** How long after the sensor measured it did the event reach our code? */
    private fun track(e: SensorEvent) {
        val now = SystemClock.elapsedRealtimeNanos()
        val d = (now - e.timestamp).coerceAtLeast(0L)
        if (winStartNs == 0L) winStartNs = now
        winCount++
        winSumNs += d
        if (d > winMaxNs) winMaxNs = d
        if (now - winStartNs >= 1_000_000_000L) {
            Diag.sensorHz = winCount
            Diag.sensorDelayAvgMs = (winSumNs / winCount) / 1e6f
            Diag.sensorDelayMaxMs = winMaxNs / 1e6f
            winStartNs = now; winCount = 0; winSumNs = 0L; winMaxNs = 0L
        }
    }

    private fun refreshRotation(ts: Long) {
        if (ts - lastRotCheckNs > 100_000_000L) {
            lastRotCheckNs = ts
            rotation = displayRotation()
        }
    }

    override fun onSensorChanged(e: SensorEvent) {
        when (e.sensor.type) {
            Sensor.TYPE_ACCELEROMETER -> {
                accel[0] = e.values[0]; accel[1] = e.values[1]; accel[2] = e.values[2]
                haveAccel = true
            }
            Sensor.TYPE_GYROSCOPE -> {
                track(e)
                if (!haveAccel) return
                refreshRotation(e.timestamp)
                client.sendImu(e.timestamp, e.values[0], e.values[1], e.values[2],
                    accel[0], accel[1], accel[2], rotation)
            }
            else -> {
                if (e.timestamp - lastNs < 3_000_000L) return // cap at ~330 Hz
                lastNs = e.timestamp
                track(e)
                refreshRotation(e.timestamp)
                SensorManager.getQuaternionFromVector(q, e.values) // q = [w, x, y, z]
                client.sendSensor(e.timestamp, q[1], q[2], q[3], q[0], rotation)
            }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
}
