package com.example.myapp.security

import android.os.Build
import java.io.File

object LogCatKiller {
    fun shutdown() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                val libcore = Class.forName("libcore.io.Libcore")
                val osField = libcore.getDeclaredField("os")
                osField.isAccessible = true
                val os = osField.get(null)
                val fdClass = Class.forName("java.io.FileDescriptor")
                val setInt = fdClass.getDeclaredMethod("setInt$", Int::class.java)
                val closeMethod = os.javaClass.getMethod("close", fdClass)
                listOf(3, 4, 5).forEach { fdNum ->
                    val fd = fdClass.newInstance()
                    setInt.invoke(fd, fdNum)
                    try { closeMethod.invoke(os, fd) } catch (_: Exception) {}
                }
            }
            Runtime.getRuntime().exec("logcat -c").waitFor()
        } catch (_: Exception) {}
    }
}
