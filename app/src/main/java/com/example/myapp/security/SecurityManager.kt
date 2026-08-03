package com.example.myapp.security

import android.content.Context

object SecurityManager {
    fun fullCheck(context: Context): Boolean {
        return NativeBridge.nativeInit(context) && NativeBridge.nativeCheck()
    }
}
