# Building ShortForge on Windows (your Acer)

ShortForge needs FFmpeg's dev libraries. The easiest route on Windows is vcpkg.

## 1. Install vcpkg (one time)

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
```

## 2. Install FFmpeg with x264

```powershell
C:\vcpkg\vcpkg install ffmpeg[x264]:x64-windows
```

This takes a while (it builds x264 + FFmpeg from source). Go make tea.

## 3. Build ShortForge

```powershell
cd C:\path\to\shortforge
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary lands at `build\Release\shortforge.exe`. The vcpkg DLLs
(`avformat-*.dll` etc.) must sit next to the exe — copy them from
`C:\vcpkg\installed\x64-windows\bin`, or add that folder to PATH.

## 4. Run

```powershell
python assets\gen_samples.py
.\build\Release\shortforge.exe probe
.\build\Release\shortforge.exe render assets\sample_project.json -o short.mp4
```

## NVENC (NVIDIA GPUs only — skip this if you have AMD)

> **AMD users (e.g. Radeon RX 560X):** NVENC is NVIDIA-only, so it never
> applies to your card — skip this whole section. The default vcpkg build
> uses libx264 on the CPU, which is the right path for you.

If your FFmpeg build includes `h264_nvenc` (the vcpkg build does **not**
by default — it needs the NVIDIA codec headers), ShortForge picks it
automatically; otherwise it uses libx264, which is still fast. To get NVENC:

```powershell
C:\vcpkg\vcpkg install ffmpeg[x264,nvcodec]:x64-windows
```

then rebuild. No code changes needed — the encoder fallback is automatic.
