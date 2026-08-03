package com.example.myapp.security

object NativeBridge {
    init {
        System.loadLibrary("security")
    }
    external fun nativeInit(app: android.content.Context): Boolean
    external fun nativeCheck(): Boolean
    external fun nativeVmpCalc(a: Int, b: Int): Int
}
