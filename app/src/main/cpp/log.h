#ifndef SECURITY_LOG_H
#define SECURITY_LOG_H

#ifdef SECURITY_DEBUG
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "SEC", __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "SEC", __VA_ARGS__)
#else
    #define LOGD(...) ((void)0)
    #define LOGE(...) ((void)0)
#endif

#endif
