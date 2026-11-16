package com.phonevr.app

import android.view.Surface
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.NetworkInterface
import java.net.Socket
import java.net.SocketTimeoutException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executors

/**
 * Connects to the PC (USB via `adb reverse`, or WiFi via UDP discovery), receives
 * H.264 frames into [VideoDecoder] and sends rotation-sensor samples back.
 * Reconnects automatically until [stop] is called.
 */
class StreamClient(
    private val cfg: Config,
    private val surface: Surface,
    private val screenW: Int,
    private val screenH: Int,
    private val refreshHz: Float,
    private val listener: Listener,
) {
    data class Config(val mode: Int, val host: String, val bitrateKbps: Int, val pacing: Boolean = true)

    interface Listener {
        fun onStatus(text: String)
        fun onStreaming(width: Int, height: Int, fps: Float)
        fun onDisconnected()
        fun onHostFound(ip: String)
    }

    companion object {
        const val MODE_AUTO = 0
        const val MODE_USB = 1
        const val MODE_WIFI = 2
    }

    @Volatile private var running = false
    @Volatile private var socket: Socket? = null
    @Volatile private var out: OutputStream? = null
    private val writeLock = Any()
    private var thread: Thread? = null
    private var skipUsbOnce = false

    private val sensorBuf = ByteBuffer.allocate(Proto.HEADER_SIZE + Proto.SENSOR_SIZE)
        .order(ByteOrder.LITTLE_ENDIAN)
    private val imuBuf = ByteBuffer.allocate(Proto.HEADER_SIZE + Proto.IMU_SIZE)
        .order(ByteOrder.LITTLE_ENDIAN)
    private val pingBuf = ByteBuffer.allocate(Proto.HEADER_SIZE + 8).order(ByteOrder.LITTLE_ENDIAN)
    private var lastPingMs = 0L
    private val rttStat = Stat { a, m -> Diag.rttAvgMs = a; Diag.rttMaxMs = m }
    private val queueStat = Stat { a, m -> Diag.queueAvgMs = a; Diag.queueMaxMs = m }
    // The PC and phone clocks differ by an unknown constant, so we only look at how much
    // a frame's arrival delay exceeds the best (smallest) delay seen recently.
    private var curMinUs = Long.MAX_VALUE
    private var prevMinUs = Long.MAX_VALUE
    private var minRotateUs = 0L
    private val smallBuf = ByteBuffer.allocate(Proto.HEADER_SIZE + 8).order(ByteOrder.LITTLE_ENDIAN)
    @Volatile private var lastIdrRequestMs = 0L

    // Android forbids network writes on the UI thread (NetworkOnMainThreadException), and
    // recenter/click come from key and touch handlers, so they are sent from this thread.
    private val sendExec = Executors.newSingleThreadExecutor { r -> Thread(r, "phonevr-send") }
    private fun post(block: () -> Unit) {
        try { sendExec.execute(block) } catch (_: Exception) {}
    }

    fun start() {
        running = true
        thread = Thread({ mainLoop() }, "phonevr-net").also { it.start() }
    }

    fun stop() {
        running = false
        out = null
        try { socket?.close() } catch (_: Exception) {}
        try { thread?.join(2000) } catch (_: InterruptedException) {}
        sendExec.shutdownNow()
    }

    // ---- outgoing messages ------------------------------------------------

    fun sendSensor(timestampNs: Long, qx: Float, qy: Float, qz: Float, qw: Float, rotation: Int) {
        val o = out ?: return
        synchronized(writeLock) {
            val b = sensorBuf
            b.clear()
            writeHeader(b, Proto.MSG_SENSOR, Proto.SENSOR_SIZE)
            b.putLong(timestampNs)
            b.putFloat(qx); b.putFloat(qy); b.putFloat(qz); b.putFloat(qw)
            b.put(rotation.toByte()); b.put(0); b.put(0); b.put(0)
            try {
                o.write(b.array(), 0, b.position())
                o.flush()
            } catch (e: IOException) {
                try { socket?.close() } catch (_: Exception) {}
            }
        }
    }

    fun sendImu(timestampNs: Long, gx: Float, gy: Float, gz: Float,
                ax: Float, ay: Float, az: Float, rotation: Int) {
        val o = out ?: return
        synchronized(writeLock) {
            val b = imuBuf
            b.clear()
            writeHeader(b, Proto.MSG_IMU, Proto.IMU_SIZE)
            b.putLong(timestampNs)
            b.putFloat(gx); b.putFloat(gy); b.putFloat(gz)
            b.putFloat(ax); b.putFloat(ay); b.putFloat(az)
            b.put(rotation.toByte()); b.put(0); b.put(0); b.put(0)
            try {
                o.write(b.array(), 0, b.position())
                o.flush()
            } catch (e: Exception) {
                try { socket?.close() } catch (_: Exception) {}
            }
        }
    }

    private fun sendPing() {
        val o = out ?: return
        synchronized(writeLock) {
            val b = pingBuf
            b.clear()
            writeHeader(b, Proto.MSG_PING, 8)
            b.putLong(System.nanoTime())
            try {
                o.write(b.array(), 0, b.position())
                o.flush()
            } catch (e: Exception) {
                try { socket?.close() } catch (_: Exception) {}
            }
        }
    }

    private fun noteVideoDelay(hostTimeUs: Long) {
        val nowUs = System.nanoTime() / 1000
        val raw = nowUs - hostTimeUs
        if (minRotateUs == 0L) minRotateUs = nowUs
        if (nowUs - minRotateUs >= 10_000_000L) {
            prevMinUs = curMinUs; curMinUs = Long.MAX_VALUE; minRotateUs = nowUs
        }
        if (raw < curMinUs) curMinUs = raw
        queueStat.add(raw - minOf(curMinUs, prevMinUs), nowUs)
    }

    fun sendRecenter() = post { sendSimple(Proto.MSG_RECENTER, 0) }
    fun sendBitrate(kbps: Int) = post { sendSimple(Proto.MSG_SET_BITRATE, kbps) }
    fun sendClick(down: Boolean) = post { sendSimple(Proto.MSG_CLICK, if (down) 1 else 0) }

    private fun requestIdr() {
        val now = System.currentTimeMillis()
        if (now - lastIdrRequestMs < 300) return
        lastIdrRequestMs = now
        sendSimple(Proto.MSG_REQUEST_IDR, -1)
    }

    /** payload < 0 => no payload; otherwise a single u32. */
    private fun sendSimple(type: Int, payload: Int) {
        val o = out ?: return
        synchronized(writeLock) {
            val b = smallBuf
            b.clear()
            writeHeader(b, type, if (payload >= 0) 4 else 0)
            if (payload >= 0) b.putInt(payload)
            try {
                o.write(b.array(), 0, b.position())
                o.flush()
            } catch (e: Exception) {
                try { socket?.close() } catch (_: Exception) {}
            }
        }
    }

    private fun writeHeader(b: ByteBuffer, type: Int, length: Int) {
        b.put(type.toByte()); b.put(0); b.putShort(0); b.putInt(length)
    }

    // ---- connection management ------------------------------------------

    private fun mainLoop() {
        while (running) {
            listener.onStatus(
                when (cfg.mode) {
                    MODE_USB -> "Waiting for USB link…\n(USB debugging on, PC running SteamVR)"
                    MODE_WIFI -> "Looking for PC on WiFi…"
                    else -> "Looking for PC (USB, then WiFi)…"
                }
            )
            val s = connect()
            if (s == null) {
                sleep(500)
                continue
            }
            socket = s
            val streamed = try { session(s) } catch (e: Exception) { false }
            out = null
            try { s.close() } catch (_: Exception) {}
            socket = null
            if (!streamed && s.inetAddress?.isLoopbackAddress == true) skipUsbOnce = true
            listener.onDisconnected()
            if (running) sleep(400)
        }
    }

    private fun connect(): Socket? {
        if (cfg.mode != MODE_WIFI) {
            if (skipUsbOnce) skipUsbOnce = false
            else tryTcp("127.0.0.1")?.let { return it }
        }
        if (cfg.mode != MODE_USB) {
            if (cfg.host.isNotBlank()) tryTcp(cfg.host.trim())?.let { return it }
            discover()?.let { ip ->
                tryTcp(ip)?.let {
                    listener.onHostFound(ip)
                    return it
                }
            }
        }
        return null
    }

    private fun tryTcp(host: String): Socket? = try {
        Socket().also {
            it.tcpNoDelay = true
            it.keepAlive = true
            it.connect(InetSocketAddress(host, Proto.TCP_PORT), 600)
        }
    } catch (_: Exception) {
        null
    }

    private fun discover(): String? {
        try {
            DatagramSocket().use { s ->
                s.broadcast = true
                s.soTimeout = 700
                val msg = Proto.DISCOVERY_REQUEST.toByteArray()
                for (target in broadcastTargets()) {
                    try { s.send(DatagramPacket(msg, msg.size, target, Proto.DISCOVERY_PORT)) } catch (_: Exception) {}
                }
                val buf = ByteArray(64)
                val p = DatagramPacket(buf, buf.size)
                s.receive(p)
                if (String(p.data, 0, p.length).startsWith(Proto.DISCOVERY_REPLY)) return p.address.hostAddress
            }
        } catch (_: SocketTimeoutException) {
        } catch (_: Exception) {
        }
        return null
    }

    private fun broadcastTargets(): List<InetAddress> {
        val list = ArrayList<InetAddress>()
        try {
            for (ni in NetworkInterface.getNetworkInterfaces()) {
                if (!ni.isUp || ni.isLoopback) continue
                for (ia in ni.interfaceAddresses) ia.broadcast?.let { list.add(it) }
            }
        } catch (_: Exception) {}
        try { list.add(InetAddress.getByName("255.255.255.255")) } catch (_: Exception) {}
        return list
    }

    // ---- one connected session -----------------------------------------------

    /** Returns true if the host accepted us (HELLO_ACK received). */
    private fun session(s: Socket): Boolean {
        val ins = s.getInputStream()
        val os = s.getOutputStream()
        s.soTimeout = 2000

        // HELLO
        val hello = ByteBuffer.allocate(Proto.HEADER_SIZE + Proto.HELLO_SIZE).order(ByteOrder.LITTLE_ENDIAN)
        writeHeader(hello, Proto.MSG_HELLO, Proto.HELLO_SIZE)
        hello.putInt(Proto.VERSION)
        hello.putInt(screenW); hello.putInt(screenH)
        hello.putFloat(refreshHz)
        hello.putInt(cfg.bitrateKbps)
        synchronized(writeLock) { os.write(hello.array(), 0, hello.position()); os.flush() }

        // HELLO_ACK (must arrive within a few seconds)
        val hdr = ByteArray(Proto.HEADER_SIZE)
        var tries = 0
        while (true) {
            try { if (!readFully(ins, hdr, 0, hdr.size)) return false; break }
            catch (_: SocketTimeoutException) { if (++tries >= 3 || !running) return false }
        }
        val hb = ByteBuffer.wrap(hdr).order(ByteOrder.LITTLE_ENDIAN)
        val type = hb.get().toInt() and 0xff
        hb.get(); hb.getShort()
        val len = hb.getInt()
        if (type != Proto.MSG_HELLO_ACK || len < Proto.ACK_SIZE || len > 4096) return false
        val ackBytes = ByteArray(len)
        if (!readFullyPatient(ins, ackBytes, 0, len)) return false
        val ab = ByteBuffer.wrap(ackBytes).order(ByteOrder.LITTLE_ENDIAN)
        val version = ab.getInt()
        val w = ab.getInt(); val h = ab.getInt()
        val fps = ab.getFloat()
        val kbps = ab.getInt()
        if (version != Proto.VERSION || w <= 0 || h <= 0) return false

        listener.onStreaming(w, h, fps)
        val decoder = VideoDecoder(surface, w, h, fps, cfg.pacing)
        out = os // sensors may flow from now on
        curMinUs = Long.MAX_VALUE; prevMinUs = Long.MAX_VALUE; minRotateUs = 0L
        var needKey = true
        var frameBuf = ByteArray(256 * 1024)
        try {
            while (running) {
                val nowMs = System.currentTimeMillis()
                if (nowMs - lastPingMs >= 250) { lastPingMs = nowMs; post { sendPing() } }
                try { if (!readFully(ins, hdr, 0, hdr.size)) break }
                catch (_: SocketTimeoutException) { continue } // SteamVR idle: no frames is normal
                val b = ByteBuffer.wrap(hdr).order(ByteOrder.LITTLE_ENDIAN)
                val t = b.get().toInt() and 0xff
                b.get(); b.getShort()
                val l = b.getInt()
                if (l < 0 || l > Proto.MAX_MESSAGE) break
                if (l > frameBuf.size) frameBuf = ByteArray(l + l / 2)
                if (!readFullyPatient(ins, frameBuf, 0, l)) break
                if (t == Proto.MSG_PONG && l >= 8) {
                    val sentNs = ByteBuffer.wrap(frameBuf, 0, 8).order(ByteOrder.LITTLE_ENDIAN).getLong()
                    val nowNs = System.nanoTime()
                    rttStat.add(((nowNs - sentNs) / 1000).coerceAtLeast(0L), nowNs / 1000)
                    continue
                }
                if (t != Proto.MSG_VIDEO || l <= Proto.VIDEO_HEADER_SIZE) continue

                val keyframe = frameBuf[16].toInt() != 0
                noteVideoDelay(ByteBuffer.wrap(frameBuf, 8, 8).order(ByteOrder.LITTLE_ENDIAN).getLong())
                if (needKey && !keyframe) { requestIdr(); continue }
                val ok = try {
                    decoder.feed(frameBuf, Proto.VIDEO_HEADER_SIZE, l - Proto.VIDEO_HEADER_SIZE,
                        System.nanoTime() / 1000)
                } catch (e: IllegalStateException) { break }
                if (!ok) { needKey = true; requestIdr() } else if (keyframe) needKey = false
            }
        } finally {
            decoder.stop()
        }
        return true
    }

    /** Reads exactly len bytes. false = stream closed. Propagates a timeout only if nothing was read yet. */
    private fun readFully(ins: InputStream, buf: ByteArray, off: Int, len: Int): Boolean {
        var got = 0
        while (got < len) {
            val n = try {
                ins.read(buf, off + got, len - got)
            } catch (e: SocketTimeoutException) {
                if (got == 0) throw e
                continue
            }
            if (n < 0) return false
            got += n
        }
        return true
    }

    /** Like readFully but never throws a timeout; gives up after ~10 s of silence mid-message. */
    private fun readFullyPatient(ins: InputStream, buf: ByteArray, off: Int, len: Int): Boolean {
        var got = 0
        var stalls = 0
        while (got < len && running) {
            val n = try {
                ins.read(buf, off + got, len - got)
            } catch (e: SocketTimeoutException) {
                if (++stalls >= 5) return false
                continue
            }
            if (n < 0) return false
            stalls = 0
            got += n
        }
        return got == len
    }

    private fun sleep(ms: Long) {
        try { Thread.sleep(ms) } catch (_: InterruptedException) {}
    }
}
