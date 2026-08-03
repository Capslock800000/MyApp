package com.example.myapp.security

import android.animation.ArgbEvaluator
import android.animation.ValueAnimator
import android.app.Activity
import android.app.ActivityManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.TextView
import com.example.myapp.R
import kotlin.random.Random

class DogeTauntActivity : Activity() {

    private val tauntMessages = arrayOf(
        "WOW", "SUCH HACK", "VERY REVERSE", "MUCH CRACK",
        "SO SMART", "BUT NO", "F*CK YOU", "TRY AGAIN",
        "404 SUCCESS", "NULL POINTER", "SKILL ISSUE", "STILL HERE?"
    )

    private val colors = arrayOf(
        Color.RED, Color.YELLOW, Color.GREEN,
        Color.CYAN, Color.BLUE, Color.MAGENTA
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_SECURE)

        setContentView(R.layout.activity_doge_taunt)

        val textView = findViewById<TextView>(R.id.tauntText)
        val btnClose = findViewById<Button>(R.id.btnClose)
        val extraText = findViewById<TextView>(R.id.extraText)

        startRainbowDisco(textView)

        Handler(Looper.getMainLooper()).postDelayed(object : Runnable {
            override fun run() {
                textView.text = tauntMessages.random()
                textView.rotation = Random.nextInt(-15, 15).toFloat()
                Handler(Looper.getMainLooper()).postDelayed(this, 800)
            }
        }, 800)

        if (isScreenRecording()) {
            extraText.visibility = View.VISIBLE
            extraText.text = "检测到屏幕录制\n建议发到酷安：'已成功破解'\n然后看评论区"
        }

        btnClose.setOnClickListener {
            textView.text = "STILL HERE?"
            btnClose.text = "再点一次 (Click Again)"
            btnClose.setOnClickListener {
                textView.text = "F*CK YOU"
                textView.textSize = 96f
                Handler(Looper.getMainLooper()).postDelayed({
                    killAppAndClearData()
                }, 3000)
            }
        }

        (getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager)
            .appTasks.forEach { it.setExcludeFromRecents(true) }
    }

    private fun startRainbowDisco(textView: TextView) {
        ValueAnimator.ofInt(0, colors.size - 1).apply {
            duration = 600
            repeatCount = ValueAnimator.INFINITE
            setEvaluator(ArgbEvaluator())
            addUpdateListener { animation ->
                val index = (animation.animatedValue as Int) % colors.size
                textView.setTextColor(colors[index])
                textView.setShadowLayer(20f, 0f, 0f, colors[(index + 3) % colors.size])
            }
            start()
        }
    }

    private fun isScreenRecording(): Boolean {
        return try {
            val dm = getSystemService(Context.DISPLAY_SERVICE) as android.hardware.display.DisplayManager
            dm.displays.size > 1 || dm.displays.any { it.flags and android.view.Display.FLAG_SECURE != 0 }
        } catch (_: Exception) { false }
    }

    private fun killAppAndClearData() {
        getSharedPreferences("config", Context.MODE_PRIVATE).edit().clear().apply()
        finishAffinity()
        android.os.Process.killProcess(android.os.Process.myPid())
    }

    override fun onBackPressed() {
        findViewById<TextView>(R.id.tauntText).text = "NO ESCAPE"
    }

    companion object {
        fun launch(context: Context) {
            context.startActivity(Intent(context, DogeTauntActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_NEW_TASK or
                        Intent.FLAG_ACTIVITY_CLEAR_TASK or
                        Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS
            })
        }
    }
}
