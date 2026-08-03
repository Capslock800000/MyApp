#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/system_properties.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <zip.h>

#include "log.h"

extern const char* EXPECTED_GOLDEN_HASH;
extern const unsigned char BUILD_KEY[32];

static std::string calcMd5Hex(const unsigned char* data, size_t len) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);
    char hex[33];
    const char* chars = "0123456789abcdef";
    for (unsigned int i = 0; i < hashLen; i++) {
        hex[i*2] = chars[(hash[i] >> 4) & 0xF];
        hex[i*2+1] = chars[hash[i] & 0xF];
    }
    hex[32] = '\0';
    return std::string(hex);
}

static std::string calcSha512Hex(const std::string& str) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha512(), nullptr);
    EVP_DigestUpdate(ctx, str.c_str(), str.length());
    EVP_DigestFinal_ex(ctx, hash, &hashLen);
    EVP_MD_CTX_free(ctx);
    char hex[129];
    const char* chars = "0123456789abcdef";
    for (unsigned int i = 0; i < hashLen; i++) {
        hex[i*2] = chars[(hash[i] >> 4) & 0xF];
        hex[i*2+1] = chars[hash[i] & 0xF];
    }
    hex[128] = '\0';
    return std::string(hex);
}

static jobject getApplication(JNIEnv* env) {
    jclass cls = env->FindClass("android/app/ActivityThread");
    jmethodID mid = env->GetStaticMethodID(cls, "currentApplication", "()Landroid/app/Application;");
    return env->CallStaticObjectMethod(cls, mid);
}

static std::string getApkPath(JNIEnv* env) {
    jobject app = getApplication(env);
    jclass appCls = env->GetObjectClass(app);
    jmethodID getPCP = env->GetMethodID(appCls, "getPackageCodePath", "()Ljava/lang/String;");
    jstring path = (jstring)env->CallObjectMethod(app, getPCP);
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    std::string result(cpath);
    env->ReleaseStringUTFChars(path, cpath);
    return result;
}

static std::vector<unsigned char> aesDecrypt(const unsigned char* data, size_t len, const unsigned char* key) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    std::vector<unsigned char> out(len + EVP_MAX_BLOCK_LENGTH);
    int outLen = 0, finalLen = 0;
    EVP_DecryptInit_ex(ctx, EVP_aes_256_ecb(), nullptr, key, nullptr);
    EVP_DecryptUpdate(ctx, out.data(), &outLen, data, (int)len);
    EVP_DecryptFinal_ex(ctx, out.data() + outLen, &finalLen);
    EVP_CIPHER_CTX_free(ctx);
    out.resize(outLen + finalLen);
    return out;
}

static bool isEmulator() {
    char value[PROP_VALUE_MAX];
    __system_property_get("ro.kernel.qemu", value);
    if (value[0] == '1') return true;
    __system_property_get("ro.hardware.vm", value);
    if (strlen(value) > 0) return true;
    if (access("/dev/socket/qemud", F_OK) == 0) return true;
    if (access("/dev/qemu_pipe", F_OK) == 0) return true;
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "hypervisor") || strstr(line, "Hypervisor")) {
                fclose(fp); return true;
            }
        }
        fclose(fp);
    }
    __system_property_get("ro.hardware", value);
    if (strstr(value, "goldfish") || strstr(value, "ranchu") || strstr(value, "vbox") || strstr(value, "generic")) return true;
    __system_property_get("ro.product.manufacturer", value);
    if (strstr(value, "Genymotion") || strstr(value, "BlueStacks") || strstr(value, "Nox") || strstr(value, "Memu")) return true;
    return false;
}

static bool checkMapsHook() {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[512];
    bool hit = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "liblsposed.so") || strstr(line, "libxposed.so") ||
            strstr(line, "libriru.so") || strstr(line, "libzygisk.so") ||
            strstr(line, "libinject.so") || strstr(line, "XposedBridge")) {
            hit = true; break;
        }
    }
    fclose(fp);
    return hit;
}

static bool checkNativeBridge() {
    char value[PROP_VALUE_MAX] = {0};
    __system_property_get("ro.dalvik.vm.native.bridge", value);
    return (strlen(value) > 0 && strcmp(value, "0") != 0);
}

static bool checkJNIEnvHooked(JNIEnv* env) {
    void* findClassPtr = (void*)env->functions->FindClass;
    Dl_info info;
    if (dladdr(findClassPtr, &info) && info.dli_fname) {
        return !(strstr(info.dli_fname, "/system/") || strstr(info.dli_fname, "/apex/"));
    }
    return false;
}

static bool checkXposedStack(JNIEnv* env) {
    jclass threadCls = env->FindClass("java/lang/Thread");
    jmethodID currentThread = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
    jobject thread = env->CallStaticObjectMethod(threadCls, currentThread);
    jmethodID getStackTrace = env->GetMethodID(threadCls, "getStackTrace", "()[Ljava/lang/StackTraceElement;");
    jobjectArray stack = (jobjectArray)env->CallObjectMethod(thread, getStackTrace);
    jsize len = env->GetArrayLength(stack);
    for (int i = 0; i < len && i < 60; i++) {
        jobject elem = env->GetObjectArrayElement(stack, i);
        jclass elemCls = env->GetObjectClass(elem);
        jmethodID getClassName = env->GetMethodID(elemCls, "getClassName", "()Ljava/lang/String;");
        jstring className = (jstring)env->CallObjectMethod(elem, getClassName);
        const char* name = env->GetStringUTFChars(className, nullptr);
        bool bad = (strstr(name, "xposed") || strstr(name, "Xposed") || strstr(name, "lsposed") || strstr(name, "LSPosed"));
        env->ReleaseStringUTFChars(className, name);
        env->DeleteLocalRef(elem);
        if (bad) return true;
    }
    return false;
}

static bool isLSPosedPresent(JNIEnv* env) {
    return checkMapsHook() || checkNativeBridge() || checkJNIEnvHooked(env) || checkXposedStack(env);
}

static bool verifyIntegrity(const char* apkPath) {
    int err = 0;
    zip_t* za = zip_open(apkPath, ZIP_RDONLY, &err);
    if (!za) return false;

    std::vector<std::string> md5List;
    FILE* fp = fopen(apkPath, "rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        std::vector<unsigned char> buf(sz);
        fread(buf.data(), 1, sz, fp);
        fclose(fp);
        md5List.push_back(calcMd5Hex(buf.data(), buf.size()));
    }

    struct Item { zip_uint64_t idx; std::string name; };
    std::vector<Item> soItems, dexItems, resItems, assetsItems;
    std::string manifestMd5, arscMd5;

    zip_int64_t num = zip_get_num_entries(za, 0);
    for (zip_int64_t i = 0; i < num; i++) {
        zip_stat_t sb;
        if (zip_stat_index(za, i, 0, &sb) != 0) continue;
        if (sb.name[sb.size - 1] == '/') continue;
        std::string name = sb.name;

        if (name == "AndroidManifest.xml") {
            zip_file_t* f = zip_fopen_index(za, i, 0);
            std::vector<char> buf(sb.size);
            zip_fread(f, buf.data(), sb.size);
            zip_fclose(f);
            manifestMd5 = calcMd5Hex((unsigned char*)buf.data(), buf.size());
        } else if (name == "resources.arsc") {
            zip_file_t* f = zip_fopen_index(za, i, 0);
            std::vector<char> buf(sb.size);
            zip_fread(f, buf.data(), sb.size);
            zip_fclose(f);
            arscMd5 = calcMd5Hex((unsigned char*)buf.data(), buf.size());
        } else if (name.rfind("lib/", 0) == 0 && name.size() > 3 && name.substr(name.size()-3) == ".so") {
            soItems.push_back({(zip_uint64_t)i, name});
        } else if (name.size() > 4 && name.substr(name.size()-4) == ".dex") {
            dexItems.push_back({(zip_uint64_t)i, name});
        } else if (name.rfind("res/", 0) == 0) {
            resItems.push_back({(zip_uint64_t)i, name});
        } else if (name.rfind("assets/", 0) == 0) {
            assetsItems.push_back({(zip_uint64_t)i, name});
        }
    }

    auto byName = [](const Item& a, const Item& b){ return a.name < b.name; };
    std::sort(soItems.begin(), soItems.end(), byName);
    std::sort(dexItems.begin(), dexItems.end(), byName);
    std::sort(resItems.begin(), resItems.end(), byName);
    std::sort(assetsItems.begin(), assetsItems.end(), byName);

    auto addHash = [&](const Item& it) {
        zip_file_t* f = zip_fopen_index(za, it.idx, 0);
        std::vector<char> buf(1024);
        std::vector<unsigned char> all;
        zip_int64_t n;
        while ((n = zip_fread(f, buf.data(), buf.size())) > 0) {
            all.insert(all.end(), buf.begin(), buf.begin() + n);
        }
        zip_fclose(f);
        md5List.push_back(calcMd5Hex(all.data(), all.size()));
    };

    if (!manifestMd5.empty()) md5List.push_back(manifestMd5);
    for (const auto& it : soItems) addHash(it);
    if (!arscMd5.empty()) md5List.push_back(arscMd5);
    for (const auto& it : dexItems) addHash(it);
    for (const auto& it : resItems) addHash(it);
    for (const auto& it : assetsItems) addHash(it);

    zip_close(za);

    std::vector<std::string> sha512List;
    for (const auto& md5 : md5List) sha512List.push_back(calcSha512Hex(md5));

    std::string combined;
    for (size_t i = 0; i < sha512List.size(); i++) {
        if (i > 0) combined += "§";
        combined += sha512List[i];
    }

    std::string golden = calcSha512Hex(combined);
    return golden == std::string(EXPECTED_GOLDEN_HASH);
}

static bool loadEncryptedDex(JNIEnv* env, jobject app) {
    jclass appCls = env->GetObjectClass(app);
    jmethodID getAssets = env->GetMethodID(appCls, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assets = env->CallObjectMethod(app, getAssets);
    AAssetManager* mgr = AAssetManager_fromJava(env, assets);

    jmethodID getDir = env->GetMethodID(appCls, "getDir", "(Ljava/lang/String;I)Ljava/io/File;");
    jstring dirName = env->NewStringUTF("dex");
    jobject dir = env->CallObjectMethod(app, getDir, dirName, 0);
    jmethodID getPath = env->GetMethodID(env->GetObjectClass(dir), "getPath", "()Ljava/lang/String;");
    jstring path = (jstring)env->CallObjectMethod(dir, getPath);
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    std::string baseDir(cpath);
    env->ReleaseStringUTFChars(path, cpath);

    std::vector<std::string> dexPaths;
    AAssetDir* assetDir = AAssetManager_openDir(mgr, "");
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != nullptr) {
        if (strncmp(filename, "enc_classes", 11) == 0 && strstr(filename, ".bin")) {
            AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_STREAMING);
            if (!asset) continue;
            off_t len = AAsset_getLength(asset);
            std::vector<unsigned char> encrypted(len);
            AAsset_read(asset, encrypted.data(), len);
            AAsset_close(asset);

            auto decrypted = aesDecrypt(encrypted.data(), encrypted.size(), BUILD_KEY);
            std::string outPath = baseDir + "/" + std::string(filename);
            outPath = outPath.substr(0, outPath.size() - 4);

            int fd = open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
            if (fd >= 0) {
                write(fd, decrypted.data(), decrypted.size());
                close(fd);
                dexPaths.push_back(outPath);
            }
        }
    }
    AAssetDir_close(assetDir);

    if (dexPaths.empty()) return false;

    std::string dexPath = dexPaths[0];
    for (size_t i = 1; i < dexPaths.size(); i++) dexPath += ":" + dexPaths[i];

    std::string optDir = baseDir + "/opt";
    mkdir(optDir.c_str(), 0700);

    jstring dexPathJ = env->NewStringUTF(dexPath.c_str());
    jstring optDirJ = env->NewStringUTF(optDir.c_str());
    jstring libDirJ = env->NewStringUTF("");

    jmethodID getClassLoader = env->GetMethodID(appCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject parentLoader = env->CallObjectMethod(app, getClassLoader);

    jclass dexLoaderCls = env->FindClass("dalvik/system/DexClassLoader");
    jmethodID ctor = env->GetMethodID(dexLoaderCls, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
    jobject dexLoader = env->NewObject(dexLoaderCls, ctor, dexPathJ, optDirJ, libDirJ, parentLoader);

    jclass threadCls = env->FindClass("java/lang/Thread");
    jmethodID currentThread = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
    jobject thread = env->CallStaticObjectMethod(threadCls, currentThread);
    jmethodID setCL = env->GetMethodID(threadCls, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
    env->CallVoidMethod(thread, setCL, dexLoader);

    return true;
}

static bool loadEncryptedSo(JNIEnv* env, jobject app) {
    jclass appCls = env->GetObjectClass(app);
    jmethodID getAssets = env->GetMethodID(appCls, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assets = env->CallObjectMethod(app, getAssets);
    AAssetManager* mgr = AAssetManager_fromJava(env, assets);

    jmethodID getDir = env->GetMethodID(appCls, "getDir", "(Ljava/lang/String;I)Ljava/io/File;");
    jstring dirName = env->NewStringUTF("libs");
    jobject dir = env->CallObjectMethod(app, getDir, dirName, 0);
    jmethodID getPath = env->GetMethodID(env->GetObjectClass(dir), "getPath", "()Ljava/lang/String;");
    jstring path = (jstring)env->CallObjectMethod(dir, getPath);
    const char* cpath = env->GetStringUTFChars(path, nullptr);
    std::string libDir(cpath);
    env->ReleaseStringUTFChars(path, cpath);

    AAssetDir* assetDir = AAssetManager_openDir(mgr, "");
    const char* filename;
    while ((filename = AAssetDir_getNextFileName(assetDir)) != nullptr) {
        if (strncmp(filename, "enc_", 4) == 0 && strstr(filename, ".bin")) {
            AAsset* asset = AAssetManager_open(mgr, filename, AASSET_MODE_STREAMING);
            if (!asset) continue;
            off_t len = AAsset_getLength(asset);
            std::vector<unsigned char> encrypted(len);
            AAsset_read(asset, encrypted.data(), len);
            AAsset_close(asset);

            auto decrypted = aesDecrypt(encrypted.data(), encrypted.size(), BUILD_KEY);
            std::string outPath = libDir + "/" + std::string(filename);
            outPath = outPath.substr(0, outPath.size() - 4);

            int fd = open(outPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0700);
            if (fd >= 0) {
                write(fd, decrypted.data(), decrypted.size());
                close(fd);
                dlopen(outPath.c_str(), RTLD_NOW);
            }
        }
    }
    AAssetDir_close(assetDir);
    return true;
}

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    if (isEmulator() || checkMapsHook() || checkNativeBridge()) return JNI_ERR;
    return JNI_VERSION_1_6;
}

JNIEXPORT jboolean JNICALL
Java_com_example_myapp_security_NativeBridge_nativeInit(JNIEnv* env, jobject, jobject app) {
    if (isEmulator() || isLSPosedPresent(env)) return JNI_FALSE;
    std::string apkPath = getApkPath(env);
    if (!verifyIntegrity(apkPath.c_str())) return JNI_FALSE;
    if (!loadEncryptedDex(env, app)) return JNI_FALSE;
    loadEncryptedSo(env, app);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_example_myapp_security_NativeBridge_nativeCheck(JNIEnv* env, jobject) {
    if (isEmulator() || checkMapsHook()) return JNI_FALSE;
    std::string apkPath = getApkPath(env);
    return verifyIntegrity(apkPath.c_str()) ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
