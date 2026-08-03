-keep class com.example.myapp.MyApplication
-keep class com.example.myapp.security.NativeBridge {
    native <methods>;
}

-overloadaggressively
-repackageclasses 'a'
-allowaccessmodification
-flattenpackagehierarchy
-obfuscationdictionary proguard-dict.txt
-classobfuscationdictionary proguard-dict.txt
-packageobfuscationdictionary proguard-dict.txt
-optimizationpasses 5

-assumenosideeffects class android.util.Log {
    public static int v(...);
    public static int d(...);
    public static int i(...);
    public static int w(...);
    public static int e(...);
}

-assumenosideeffects class java.io.PrintStream {
    public void println(...);
    public void print(...);
}

-assumenosideeffects class java.lang.Throwable {
    public void printStackTrace();
}

-renamesourcefileattribute a
-keepattributes !SourceFile,!LineNumberTable
