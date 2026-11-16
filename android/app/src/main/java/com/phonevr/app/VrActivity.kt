package com.phonevr.app

import android.app.Activity
import android.content.Context
import android.content.pm.ActivityInfo
import android.graphics.Color
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.util.DisplayMetrics
import android.view.GestureDetector
import android.view.Gravity
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import kotlin.math.min

/**
 * Fullscreen landscape view: a plain SurfaceView that the hardware decoder
 * draws into. Deliberately no GL and no VR SDK.
 */
class VrActivity : Activity(), SurfaceHolder.Callback, StreamClient.Listener {

    private lateinit var prefs: Prefs
    private lateinit var surfaceView: SurfaceView
    private lateinit var status: TextView
    private var client: StreamClient? = null
    private var sensors: SensorSender? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var screenW = 0
    private var screenH = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        requestedOrientation = if (prefs.reverseLandscape)
            ActivityInfo.SCREEN_ORIENTATION_REVERSE_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        try {
            val pm = getSystemService(Context.POWER_SERVICE) as PowerManager
            if (pm.isSustainedPerformanceModeSupported) window.setSustainedPerformanceMode(true)
        } catch (_: Exception) {}

        val root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        surfaceView = SurfaceView(this)
        root.addView(surfaceView, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.CENTER))
        status = TextView(this).apply {
            setTextColor(Color.WHITE); textSize = 18f; gravity = Gravity.CENTER
        }
        root.addView(status, FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER))
        setContentView(root)

        val detector = GestureDetector(this, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean { client?.sendRecenter(); return true }
        })
        surfaceView.setOnTouchListener { _, ev -> detector.onTouchEvent(ev); true }
        surfaceView.holder.addCallback(this)

        try {
            val wm = applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
            val mode = if (Build.VERSION.SDK_INT >= 29) WifiManager.WIFI_MODE_FULL_LOW_LATENCY
            else WifiManager.WIFI_MODE_FULL_HIGH_PERF
            wifiLock = wm.createWifiLock(mode, "phonevr").also { it.acquire() }
        } catch (_: Exception) {}
    }

    @Suppress("DEPRECATION")
    private fun currentRotation(): Int = windowManager.defaultDisplay.rotation

    override fun surfaceCreated(holder: SurfaceHolder) {
        val dm = DisplayMetrics()
        @Suppress("DEPRECATION") windowManager.defaultDisplay.getRealMetrics(dm)
        screenW = maxOf(dm.widthPixels, dm.heightPixels)
        screenH = minOf(dm.widthPixels, dm.heightPixels)
        @Suppress("DEPRECATION") val hz = windowManager.defaultDisplay.refreshRate

        val c = StreamClient(
            StreamClient.Config(prefs.mode, prefs.host, prefs.bitrateKbps),
            holder.surface, screenW, screenH, hz, this)
        client = c
        val s = SensorSender(this, c) { currentRotation() }
        if (!s.start()) {
            setStatus("This phone has no gyroscope-based rotation sensor.")
            return
        }
        sensors = s
        c.start()
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        sensors?.stop(); sensors = null
        client?.stop(); client = null
    }

    override fun onDestroy() {
        try { wifiLock?.release() } catch (_: Exception) {}
        super.onDestroy()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (!hasFocus) return
        if (Build.VERSION.SDK_INT >= 30) {
            window.insetsController?.hide(WindowInsets.Type.systemBars())
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN or
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
        }
    }

    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_DOWN -> {
                if (event.repeatCount == 0) client?.sendRecenter()
                return true
            }
            KeyEvent.KEYCODE_VOLUME_UP -> {
                if (event.repeatCount == 0) client?.sendRecenter()
                return true
            }
        }
        return super.onKeyDown(keyCode, event)
    }

    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_VOLUME_UP -> { client?.sendClick(false); return true }
            KeyEvent.KEYCODE_VOLUME_DOWN -> return true
        }
        return super.onKeyUp(keyCode, event)
    }

    // ---- StreamClient.Listener (called from the network thread) ----------

    private fun setStatus(t: String) = runOnUiThread {
        status.text = t
        status.visibility = if (t.isEmpty()) View.GONE else View.VISIBLE
    }

    override fun onStatus(text: String) = setStatus(text)

    override fun onStreaming(width: Int, height: Int, fps: Float) {
        runOnUiThread {
            status.visibility = View.GONE
            // Keep the video's pixel aspect ratio (letterbox if the PC stream differs from the screen).
            val scale = min(screenW.toFloat() / width, screenH.toFloat() / height)
            val lp = surfaceView.layoutParams as FrameLayout.LayoutParams
            lp.width = (width * scale).toInt()
            lp.height = (height * scale).toInt()
            lp.gravity = Gravity.CENTER
            surfaceView.layoutParams = lp
            if (Build.VERSION.SDK_INT >= 30) {
                try {
                    surfaceView.holder.surface.setFrameRate(
                        fps, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE)
                } catch (_: Exception) {}
            }
        }
    }

    override fun onDisconnected() = setStatus("Disconnected. Reconnecting…")

    override fun onHostFound(ip: String) {
        if (prefs.host.isBlank()) prefs.host = ip
    }
}
