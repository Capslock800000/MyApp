import org.jetbrains.kotlin.gradle.dsl.JvmTarget
import java.security.MessageDigest
import java.util.zip.ZipFile
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.plugin.compose")
}

android {
    namespace = "com.example.myapp"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.example.myapp"
        minSdk = 28
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        externalNativeBuild {
            cmake {
                cppFlags += "-O2 -fvisibility=hidden -fvisibility-inlines-hidden -std=c++17"
                arguments += "-DOPENSSL_ROOT_DIR=/usr"
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
    }

    externalNativeBuild {
        cmake {
            path = file("CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.graphics.shapes)
    implementation(libs.androidx.ui)
    implementation(libs.androidx.ui.graphics)
    implementation(libs.androidx.ui.tooling.preview)
}

val encryptKey = "BuildTimeKey_${System.currentTimeMillis()}"

fun md5Hex(bytes: ByteArray): String =
    MessageDigest.getInstance("MD5").digest(bytes).joinToString("") { "%02x".format(it) }

fun sha512Hex(str: String): String =
    MessageDigest.getInstance("SHA-512").digest(str.toByteArray()).joinToString("") { "%02x".format(it) }

// ==================== 安全构建任务 ====================

tasks.register<DefaultTask>("encryptDexRelease") {
    group = "security"
    doLast {
        val dexDir = layout.buildDirectory.dir("intermediates/dex/release").get().asFile
        if (!dexDir.exists()) {
            println("[Security] Dex dir not found, skipping dex encryption")
            return@doLast
        }
        val assetsDir = layout.buildDirectory.dir("intermediates/assets/release/mergeReleaseAssets/out").get().asFile
        assetsDir.mkdirs()
        val key = MessageDigest.getInstance("SHA-256").digest(encryptKey.toByteArray())
        dexDir.walk().filter { it.name.endsWith(".dex") }.sortedBy { it.name }.forEach { dex ->
            val cipher = Cipher.getInstance("AES/ECB/PKCS5Padding")
            cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"))
            val enc = cipher.doFinal(dex.readBytes())
            File(assetsDir, "enc_${dex.name}.bin").writeBytes(enc)
            println("[Security] Encrypted ${dex.name}")
        }
    }
}

tasks.register<DefaultTask>("encryptSoRelease") {
    group = "security"
    doLast {
        val libsDir = layout.buildDirectory.dir("intermediates/merged_native_libs/release/out/lib").get().asFile
        if (!libsDir.exists()) {
            println("[Security] Native libs dir not found, skipping so encryption")
            return@doLast
        }
        val assetsDir = layout.buildDirectory.dir("intermediates/assets/release/mergeReleaseAssets/out").get().asFile
        assetsDir.mkdirs()
        val key = MessageDigest.getInstance("SHA-256").digest(encryptKey.toByteArray())
        libsDir.walk().filter { it.name.endsWith(".so") }.forEach { so ->
            val cipher = Cipher.getInstance("AES/ECB/PKCS5Padding")
            cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"))
            val enc = cipher.doFinal(so.readBytes())
            File(assetsDir, "enc_${so.nameWithoutExtension}.bin").writeBytes(enc)
            println("[Security] Encrypted ${so.name}")
        }
    }
}

tasks.register<JavaExec>("obfuscateFlowRelease") {
    group = "security"
    dependsOn("compileReleaseKotlin")
    val inputDir = layout.buildDirectory.dir("tmp/kotlin-classes/release").get().asFile
    val outputDir = layout.buildDirectory.dir("intermediates/obf/release").get().asFile
    outputDir.mkdirs()
    classpath = project.files(
        configurations.getByName("compileClasspath").files.filter { it.name.contains("asm") }
    )
    mainClass.set("com.example.myapp.build.Obfuscator")
    args = listOf(inputDir.absolutePath, outputDir.absolutePath, encryptKey)
}

tasks.register<DefaultTask>("generateGoldenHash") {
    group = "security"
    dependsOn("packageRelease")
    doLast {
        val apk = layout.buildDirectory.dir("outputs/apk/release").get().file("app-release-unsigned.apk").asFile
        if (!apk.exists()) return@doLast
        val md5List = mutableListOf<String>()
        md5List.add(md5Hex(apk.readBytes()))
        ZipFile(apk).use { zip ->
            zip.getEntry("AndroidManifest.xml")?.let { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
            zip.entries().toList().filter { it.name.startsWith("lib/") && it.name.endsWith(".so") && !it.isDirectory }
                .sortedBy { it.name }.forEach { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
            zip.getEntry("resources.arsc")?.let { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
            zip.entries().toList().filter { it.name.endsWith(".dex") && !it.isDirectory }
                .sortedBy { it.name }.forEach { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
            zip.entries().toList().filter { it.name.startsWith("res/") && !it.isDirectory }
                .sortedBy { it.name }.forEach { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
            zip.entries().toList().filter { it.name.startsWith("assets/") && !it.isDirectory }
                .sortedBy { it.name }.forEach { md5List.add(md5Hex(zip.getInputStream(it).readBytes())) }
        }
        val sha512List = md5List.map { sha512Hex(it) }
        val combined = sha512List.joinToString("§")
        val golden = sha512Hex(combined)
        println("\n========== GOLDEN HASH ==========")
        println(golden)
        println("=================================\n")
        val cppDir = file("src/main/cpp")
        cppDir.mkdirs()
        val keyBytes = encryptKey.toByteArray()
        val keyArray = keyBytes.take(32).joinToString(",") { "0x%02x".format(it) }
        File(cppDir, "golden_hash.cpp").writeText(
            """
            const char* EXPECTED_GOLDEN_HASH = "$golden";
            const unsigned char BUILD_KEY[32] = {$keyArray};
            """.trimIndent()
        )
        val outDir = layout.buildDirectory.dir("outputs/security").get().asFile
        outDir.mkdirs()
        File(outDir, "golden_hash.txt").writeText(golden)
    }
}

// ==================== 修复：whenTaskAdded 延迟挂钩 ====================
tasks.whenTaskAdded {
    when (name) {
        "mergeReleaseAssets" -> {
            dependsOn("encryptDexRelease", "encryptSoRelease")
            doFirst {
                val assetsDir = layout.buildDirectory.dir("intermediates/assets/release/mergeReleaseAssets/out").get().asFile
                assetsDir.mkdirs()
                val invisibleNames = listOf(
                    "\u200B", "\u200C", "\u200D", "\u2060", "\uFEFF",
                    "\u180E", "\u200E", "\u200F", "\u202A", "\u202B",
                    "\u202C", "\u202D", "\u202E", "\u2061", "\u2062",
                    "\u2063", "\u2064", "\u206A", "\u206B", "\u206C",
                    "\u206D", "\u206E", "\u206F"
                )
                repeat(1000) { idx ->
                    val name = buildString {
                        repeat(5) { append(invisibleNames.random()) }
                        append("_$idx.bin")
                    }
                    File(assetsDir, name).writeBytes(byteArrayOf())
                }
                println("[Security] Injected 1000 invisible files via doFirst")
            }
        }
        "assembleRelease" -> {
            finalizedBy("generateGoldenHash")
        }
    }
}
