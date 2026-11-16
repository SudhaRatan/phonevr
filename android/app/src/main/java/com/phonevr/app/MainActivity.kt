package com.phonevr.app

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView

/** Small settings screen built in code (no layout XML, no AndroidX). */
class MainActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = Prefs(this)
        val pad = (16 * resources.displayMetrics.density).toInt()

        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        fun label(t: String, size: Float = 14f) = TextView(this).apply {
            text = t; textSize = size; setTextColor(Color.WHITE); setPadding(0, pad / 2, 0, pad / 4)
        }

        col.addView(label("PhoneVR", 26f))
        col.addView(label("Start SteamVR on the PC first, then press Start and put the phone in your headset.\n" +
            "All rendering happens on the PC; this app only decodes video and reports head rotation."))

        col.addView(label("Connection"))
        val group = RadioGroup(this)
        val rAuto = RadioButton(this).apply { text = "Auto (USB first, then WiFi)"; id = 100; setTextColor(Color.WHITE) }
        val rUsb = RadioButton(this).apply { text = "USB only (needs USB debugging)"; id = 101; setTextColor(Color.WHITE) }
        val rWifi = RadioButton(this).apply { text = "WiFi only"; id = 102; setTextColor(Color.WHITE) }
        group.addView(rAuto); group.addView(rUsb); group.addView(rWifi)
        group.check(100 + prefs.mode)
        col.addView(group)

        val hostEdit = EditText(this).apply {
            hint = "PC IP address (optional, auto-discovered if empty)"
            setText(prefs.host)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            isSingleLine = true
            setTextColor(Color.WHITE)
            setHintTextColor(Color.GRAY)
        }
        col.addView(hostEdit)

        val brLabel = label("")
        val seek = SeekBar(this).apply { max = 20; progress = prefs.bitrateKbps / 5000 }
        fun refreshLabel() {
            brLabel.text = if (seek.progress == 0) "Video bitrate: PC default"
            else "Video bitrate: ${seek.progress * 5} Mbps (lower = more compression, less bandwidth)"
        }
        refreshLabel()
        seek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(s: SeekBar?, p: Int, u: Boolean) = refreshLabel()
            override fun onStartTrackingTouch(s: SeekBar?) {}
            override fun onStopTrackingTouch(s: SeekBar?) {}
        })
        col.addView(brLabel)
        col.addView(seek)

        val reverse = CheckBox(this).apply {
            text = "Flip landscape (phone's top edge points to the right in the holder)"
            isChecked = prefs.reverseLandscape
            setTextColor(Color.WHITE)
        }
        col.addView(reverse)

        val rawGyro = CheckBox(this).apply {
            text = "Raw gyroscope mode (try this if head tracking feels laggy; the PC does the sensor fusion)"
            isChecked = prefs.rawGyro
            setTextColor(Color.WHITE)
        }
        col.addView(rawGyro)

        val diag = CheckBox(this).apply {
            text = "Show diagnostics overlay (latency numbers, top-left of the screen)"
            isChecked = prefs.showDiag
            setTextColor(Color.WHITE)
        }
        col.addView(diag)

        val sustained = CheckBox(this).apply {
            text = "Sustained performance mode (cooler; untick to test for lower latency)"
            isChecked = prefs.sustainedPerf
            setTextColor(Color.WHITE)
        }
        col.addView(sustained)

        val pacing = CheckBox(this).apply {
            text = "Frame pacing (fixes extra screen delay on some phones; untick to compare)"
            isChecked = prefs.framePacing
            setTextColor(Color.WHITE)
        }
        col.addView(pacing)

        col.addView(label("In VR: volume up/down or double-tap = recenter. Back = exit."))

        val start = Button(this).apply {
            text = "Start"
            setOnClickListener {
                prefs.mode = group.checkedRadioButtonId - 100
                prefs.host = hostEdit.text.toString().trim()
                prefs.bitrateKbps = seek.progress * 5000
                prefs.reverseLandscape = reverse.isChecked
                prefs.rawGyro = rawGyro.isChecked
                prefs.showDiag = diag.isChecked
                prefs.sustainedPerf = sustained.isChecked
                prefs.framePacing = pacing.isChecked
                startActivity(Intent(this@MainActivity, VrActivity::class.java))
            }
        }
        col.addView(start)

        setContentView(ScrollView(this).apply {
            setBackgroundColor(Color.rgb(24, 24, 24))
            addView(col, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT))
        })
    }
}
