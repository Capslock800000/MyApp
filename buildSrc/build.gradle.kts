plugins {
    `kotlin-dsl`
}

repositories {
    mavenCentral()
    google()
}

dependencies {
    implementation("org.ow2.asm:asm:9.7")
    implementation("org.ow2.asm:asm-commons:9.7")
    implementation("org.smali:dexlib2:2.5.2") {
        // 关键修复：排除 dexlib2 自带的旧版 Guava
        exclude(group = "com.google.guava", module = "guava")
    }
    // 强制使用新版 Guava，覆盖所有传递依赖
    implementation("com.google.guava:guava:33.3.1-jre")
}
