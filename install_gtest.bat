@echo off
set PATH=C:\Program Files\msys64\ucrt64\bin;%PATH%
set VCPKG_FORCE_SYSTEM_BINARIES=1
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
"C:\vcpkg\vcpkg.exe" install gtest:x64-windows
