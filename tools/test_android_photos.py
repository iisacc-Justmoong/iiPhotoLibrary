#!/usr/bin/env python3
"""Compile and run the isolated native MediaStore probe on an already booted emulator."""
from pathlib import Path
import os
import subprocess
import zipfile

SOURCE = Path(__file__).resolve().parents[1]
BUILD = SOURCE / "build/android-probe"
SDK = Path(os.environ["ANDROID_SDK_ROOT"])
JAVA = Path(os.environ["JAVA_HOME"]) / "bin"
TOOLS = SDK / "build-tools/36.0.0"
PACKAGE = "com.iisacc.iiphotolibrary.tests"

def run(*args):
    return subprocess.run([str(arg) for arg in args], cwd=SOURCE, check=True, text=True)

def main():
    for name in ("classes", "dex", "assets"):
        (BUILD / name).mkdir(parents=True, exist_ok=True)
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "color=c=blue:s=64x64:d=0.5", "-c:v", "libx264", "-pix_fmt", "yuv420p", BUILD / "assets/video.mp4")
    jar = SDK / "platforms/android-36/android.jar"
    run(JAVA / "javac", "--release", "8", "-classpath", jar, "-d", BUILD / "classes",
        SOURCE / "platform/android/src/com/iisacc/iiphotolibrary/PhotoLibrary.java", SOURCE / "tests/android/PhotoInstrumentation.java")
    run(JAVA / "jar", "--create", "--file", BUILD / "classes.jar", "-C", BUILD / "classes", ".")
    run(JAVA / "java", "-cp", TOOLS / "lib/d8.jar", "com.android.tools.r8.D8", "--lib", jar, "--min-api", "28", "--output", BUILD / "dex", BUILD / "classes.jar")
    run(TOOLS / "aapt2", "link", "-I", jar, "--manifest", SOURCE / "tests/android/AndroidManifest.xml", "-A", BUILD / "assets", "-o", BUILD / "unsigned.apk")
    with zipfile.ZipFile(BUILD / "unsigned.apk", "a") as apk:
        apk.write(BUILD / "dex/classes.dex", "classes.dex")
    keystore = BUILD / "test.keystore"
    if not keystore.exists():
        run(JAVA / "keytool", "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android", "-alias", "test", "-keyalg", "RSA", "-validity", "30", "-dname", "CN=Society Photo Tests")
    run(JAVA / "java", "-jar", TOOLS / "lib/apksigner.jar", "sign", "--ks", keystore, "--ks-pass", "pass:android", "--out", BUILD / "probe.apk", BUILD / "unsigned.apk")
    adb = SDK / "platform-tools/adb"
    serial = os.environ.get("IIPHOTOLIBRARY_TEST_DEVICE", "emulator-5554")
    if not serial.startswith("emulator-"):
        raise SystemExit("This fixture runner only installs on an emulator.")
    run(adb, "-s", serial, "install", "-r", "-g", BUILD / "probe.apk")
    try:
        result = subprocess.run([str(adb), "-s", serial, "shell", "am", "instrument", "-w",
            PACKAGE + "/com.iisacc.iiphotolibrary.tests.PhotoInstrumentation"],
            cwd=SOURCE, check=True, text=True, capture_output=True)
        print(result.stdout, end="", flush=True)
        print(result.stderr, end="", flush=True)
        if "INSTRUMENTATION_RESULT: result=PASS" not in result.stdout or "INSTRUMENTATION_CODE: -1" not in result.stdout:
            raise SystemExit("The native MediaStore probe failed.")
    finally:
        run(adb, "-s", serial, "uninstall", PACKAGE)

if __name__ == "__main__":
    main()
