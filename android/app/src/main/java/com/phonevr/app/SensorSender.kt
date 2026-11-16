package com.phonevr.app

import android.content.Context
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.Handler
import android.os.HandlerThread
import android.os.Process

/**
 * Reports the phone's fused orientation (Android's own sensor fusion, done in
 * the sensor hub / OS, essentially free). No Cardboard, no rendering.
 * All the head-pose maths (holder mapping, recentering, prediction input) is
 * done on the PC.
 */
class SensorSender(
    context: Context,
    private val client: StreamClient,
    private val displayRotation: () -> Int,
) : SensorEventListener {

    private val sm = context.getSystemService(Context.SENSOR_SERVICE) as SensorManager
    private val thread = HandlerThread("phonevr-sensors", Process.THREAD_PRIORITY_URGENT_DISPLAY)
    private val q = FloatArray(4)
    private var lastNs = 0L
    private var lastRotCheckNs = 0L
    private var rotation = 0

    fun start(): Boolean {
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

    override fun onSensorChanged(e: SensorEvent) {
        if (e.timestamp - lastNs < 3_000_000L) return // cap at ~330 Hz
        lastNs = e.timestamp
        if (e.timestamp - lastRotCheckNs > 100_000_000L) {
            lastRotCheckNs = e.timestamp
            rotation = displayRotation()
        }
        SensorManager.getQuaternionFromVector(q, e.values) // q = [w, x, y, z]
        client.sendSensor(e.timestamp, q[1], q[2], q[3], q[0], rotation)
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}
}
