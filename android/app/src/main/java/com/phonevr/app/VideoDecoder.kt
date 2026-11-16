package com.phonevr.app

import android.media.MediaCodec
import android.media.MediaFormat
import android.view.Surface

/**
 * Hardware H.264 decoder that renders straight into the Surface. There is no
 * OpenGL, no distortion pass and no extra copy: the PC already applied the
 * lens correction, so the phone's GPU/CPU stay almost idle (less heat).
 */
class VideoDecoder(surface: Surface, width: Int, height: Int, fps: Float) {
    private val codec: MediaCodec
    @Volatile private var running = true
    private val outputThread: Thread

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
                // If several frames are ready, show only the newest one: lower latency.
                while (true) {
                    val next = codec.dequeueOutputBuffer(info2, 0)
                    if (next >= 0) {
                        codec.releaseOutputBuffer(index, false)
                        index = next
                    } else {
                        break
                    }
                }
                codec.releaseOutputBuffer(index, true) // render immediately
            }
        } catch (_: Exception) {
            // codec stopped while we were waiting; the session loop handles recovery
        }
    }

    fun stop() {
        running = false
        try { outputThread.join(500) } catch (_: InterruptedException) {}
        try { codec.stop() } catch (_: Exception) {}
        try { codec.release() } catch (_: Exception) {}
    }
}
