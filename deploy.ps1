# Build libmain.so (WSL), package the APK (Gradle) and install + start it on the Googlebook (ADB).
# Usage: .\deploy.ps1 [-SkipNative]
param([switch]$SkipNative, [string]$Serial)
$ErrorActionPreference = "Stop"
$base = "Z:\Prusa Slicer Googlebook OS\android-native"

if (-not $SkipNative) {
    wsl.exe -d ps-build -u root -- bash "/mnt/z/Prusa Slicer Googlebook OS/android-native/build-app.sh"
    if ($LASTEXITCODE -ne 0) { throw "native build failed, see app.log" }
}
wsl.exe -d ps-build -u root -- bash "/mnt/z/Prusa Slicer Googlebook OS/android-native/collect-libs.sh" | Select-Object -Last 3

$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
Push-Location "$base\app-project"
$ErrorActionPreference = "Continue"  # javac notes on stderr are no errors
& "$env:USERPROFILE\.gradle\wrapper\dists\gradle-8.14.3-bin\cv11ve7ro1n3o1j4so8xd9n66\gradle-8.14.3\bin\gradle.bat" assembleRelease --no-daemon -q 2>&1 | Where-Object { "$_" -notmatch "^Hinweis" }
$ErrorActionPreference = "Stop"
Pop-Location
if ($LASTEXITCODE -ne 0) { throw "gradle failed" }

# The Googlebook announces itself via mDNS; its wireless-debugging port changes after sleep.
$serial = $Serial
if (-not $serial) {
    $serial = (adb devices | Select-String "_adb-tls-connect.*\sdevice$" | ForEach-Object { ($_ -split "\s+")[0] } | Select-Object -First 1)
}
if (-not $serial) { throw "Googlebook not connected (enable wireless debugging)" }
$apk = "$base\app-project\app\build\outputs\apk\release\app-release.apk"
adb -s $serial install -r $apk
adb -s $serial shell appops set com.prusa3d.slicer.gbos MANAGE_EXTERNAL_STORAGE allow
adb -s $serial logcat -c
adb -s $serial shell am start -n com.prusa3d.slicer.gbos/com.prusa3d.slicer.PrusaSlicerActivity
Write-Output "DEPLOYED to $serial"
