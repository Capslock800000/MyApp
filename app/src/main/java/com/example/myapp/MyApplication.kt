package com.example.myapp

import android.app.Activity
import android.app.Application
import android.os.Bundle
import com.example.myapp.security.LogCatKiller
import com.example.myapp.security.NativeBridge

class MyApplication : Application() {

    override fun attachBaseContext(base: android.content.Context?) {
        super.attachBaseContext(base)
        LogCatKiller.shutdown()
        if (!NativeBridge.nativeInit(this)) {
            android.os.Process.killProcess(android.os.Process.myPid())
            return
        }
    }

    override fun onCreate() {
        super.onCreate()
        if (!NativeBridge.nativeCheck()) {
            punish()
            return
        }
        registerActivityLifecycleCallbacks(object : ActivityLifecycleCallbacks {
            override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {
                if (!NativeBridge.nativeCheck()) punish()
            }
            override fun onActivityStarted(activity: Activity) {
                if (!NativeBridge.nativeCheck()) punish()
            }
            override fun onActivityResumed(activity: Activity) {}
            override fun onActivityPaused(activity: Activity) {}
            override fun onActivityStopped(activity: Activity) {}
            override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}
            override fun onActivityDestroyed(activity: Activity) {}
        })
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(object : Runnable {
            override fun run() {
                if (!NativeBridge.nativeCheck()) punish()
                else android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(this, 30000)
            }
        }, 30000)
    }

    private fun punish() {
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
            throw RuntimeException("signal 11 (SIGSEGV)")
        }, kotlin.random.Random.nextLong(5000, 30000))
    }
}
