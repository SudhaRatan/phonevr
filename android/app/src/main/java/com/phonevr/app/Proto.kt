package com.phonevr.app

/** Wire protocol constants. Must match host/src/protocol.h. All integers little-endian. */
object Proto {
    const val VERSION = 1
    const val TCP_PORT = 9943
    const val DISCOVERY_PORT = 9944
    const val DISCOVERY_REQUEST = "PHONEVR_DISCOVER"
    const val DISCOVERY_REPLY = "PHONEVR_HOST"

    const val MSG_HELLO = 1
    const val MSG_SENSOR = 2
    const val MSG_RECENTER = 3
    const val MSG_SET_BITRATE = 4
    const val MSG_REQUEST_IDR = 5
    const val MSG_CLICK = 6
    const val MSG_IMU = 7
    const val MSG_PING = 8
    const val MSG_HELLO_ACK = 101
    const val MSG_VIDEO = 102
    const val MSG_PONG = 103

    const val HEADER_SIZE = 8
    const val HELLO_SIZE = 20
    const val ACK_SIZE = 24
    const val SENSOR_SIZE = 28
    const val IMU_SIZE = 36
    const val VIDEO_HEADER_SIZE = 24
    const val MAX_MESSAGE = 8 * 1024 * 1024
}
