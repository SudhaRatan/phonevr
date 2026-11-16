package com.phonevr.app

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.Handler
import android.os.HandlerThread
import android.os.Looper
import android.os.Process
import android.view.Choreographer
import java.util.concurrent.atomic.AtomicInteger
import android.view.Surface

/**
 * Hardware H.264 decoder that renders straight into the Surface. There is no
 * OpenGL, no distortion pass and no extra copy: the PC already applied the
 * lens correction, so the phone's GPU/CPU stay almost idle (less heat).
 */
/**
 * @param pacing true = hold the newest decoded frame and hand it to the screen once per display
 *   refresh, dropping older ones. Without it, a stream at the display's own refresh rate can build up
 *   a standing backlog of 2-3 frames in the display queue that never drains (= constant extra latency).
 */
class VideoDecoder(surface: Surface, width: Int, height: Int, fps: Float, private val pacing: Boolean) {
    private val codec: MediaCodec
    @Volatile private var running = true
    private val outputThread: Thread
    private val pending = AtomicInteger(-1)   // newest decoded, not yet shown output buffer
    private var vsyncThread: HandlerThread? = null
    private val renderStat = Stat { a, m -> Diag.renderAvgMs = a; Diag.renderMaxMs = m }
    private var winStartUs = 0L
    private var winCount = 0
    private var winSumUs = 0L
    private var winMaxUs = 0L

    init {
        val fmt = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height)
        // All of these are hints; unknown keys are ignored by codecs that do not support them.
        fmt.setInteger(MediaFormat.KEY_LOW_LATENCY, 1)
        fmt.setInteger(MediaFormat.KEY_PRIORITY, 0) // real-time
        fmt.setInteger(MediaFormat.KEY_OPERATING_RATE, fps.toInt().coerceAtLeast(30))
        try { fmt.setInteger("vendor.qti-ext-dec-low-latency.enable", 1) } catch (_: Exception) {}
        try { fmt.setInteger("vendor.rtc-ext-dec-low-latency.enable", 1) } catch (_: Exception) {}

        codec = MediaCodec.createDecoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(fmt, surface, null, 0)
        codec.start()
        if (pacing) {
            val t = HandlerThread("phonevr-vsync", Process.THREAD_PRIORITY_URGENT_DISPLAY)
            t.start()
            vsyncThread = t
            Handler(t.looper).post {
                val choreographer = Choreographer.getInstance()
                val callback = object : Choreographer.FrameCallback {
                    override fun doFrame(frameTimeNanos: Long) {
                        if (!running) return
                        val idx = pending.getAndSet(-1)
                        if (idx >= 0) {
                            try { codec.releaseOutputBuffer(idx, true) } catch (_: Exception) {}
                        }
                        choreographer.postFrameCallback(this)
                    }
                }
                choreographer.postFrameCallback(callback)
            }
        }
        // Tells us when each frame was actually put on screen (includes the wait for the display's refresh).
        codec.setOnFrameRenderedListener({ _, presentationTimeUs, nanoTime ->
            val nowUs = nanoTime / 1000
            renderStat.add((nowUs - presentationTimeUs).coerceAtLeast(0L), nowUs)
        }, Handler(Looper.getMainLooper()))

        outputThread = Thread({ drainLoop() }, "phonevr-decoder-out").also {
            it.priority = Thread.MAX_PRIORITY
            it.start()
        }
    }

    /** Queues one H.264 access unit. Returns false if the decoder had no free input buffer. */
    fun feed(data: ByteArray, offset: Int, length: Int, ptsUs: Long): Boolean {
        val index = codec.dequeueInputBuffer(15_000)
        if (index < 0) return false
        val buf = codec.getInputBuffer(index) ?: return false
        buf.clear()
        if (length > buf.capacity()) {
            codec.queueInputBuffer(index, 0, 0, ptsUs, 0)
            return false
        }
        buf.put(data, offset, length)
        codec.queueInputBuffer(index, 0, length, ptsUs, 0)
        return true
    }

    private fun drainLoop() {
        val info = MediaCodec.BufferInfo()
        val info2 = MediaCodec.BufferInfo()
        try {
            while (running) {
                var index = codec.dequeueOutputBuffer(info, 10_000)
                if (index < 0) continue
                var pts = info.presentationTimeUs
                // If several frames are ready, show only the newest one: lower latency.
                while (true) {
                    val next = codec.dequeueOutputBuffer(info2, 0)
                    if (next >= 0) {
                        codec.releaseOutputBuffer(index, false)
                        index = next
                        pts = info2.presentationTimeUs
                    } else {
                        break
                    }
                }
                recordLatency(pts)
                if (pacing) {
                    val old = pending.getAndSet(index)
                    if (old >= 0) codec.releaseOutputBuffer(old, false) // the newer frame replaces a stale one
                } else {
                    codec.releaseOutputBuffer(index, true) // render immediately
                }
            }
        } catch (_: Exception) {
            // codec stopped while we were waiting; the session loop handles recovery
        }
    }

    /** pts is the System.nanoTime()/1000 stamp given to feed(); the difference is time spent in the decoder. */
    private fun recordLatency(pts: Long) {
        val nowUs = System.nanoTime() / 1000
        val d = (nowUs - pts).coerceAtLeast(0L)
        if (winStartUs == 0L) winStartUs = nowUs
        winCount++
        winSumUs += d
        if (d > winMaxUs) winMaxUs = d
        if (nowUs - winStartUs >= 1_000_000L) {
            Diag.decodeAvgMs = (winSumUs / winCount) / 1000f
            Diag.decodeMaxMs = winMaxUs / 1000f
            winStartUs = nowUs; winCount = 0; winSumUs = 0L; winMaxUs = 0L
        }
    }

    fun stop() {
        running = false
        vsyncThread?.quitSafely()
        try { outputThread.join(500) } catch (_: InterruptedException) {}
        try { codec.stop() } catch (_: Exception) {}
        try { codec.release() } catch (_: Exception) {}
    }
}
