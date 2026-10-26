@echo off
:: Build encoder-compare.exe from the recorder's FFRecorder and static Windows FFmpeg (lib-build\ffmpeg-static-win).
:: The exe links the C runtime and FFmpeg statically, so it can be copied to other Windows PCs.
setlocal enableextensions
cd /d "%~dp0"

where cl >nul 2>nul
if not errorlevel 1 goto build
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" goto novs
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if not defined VSDIR goto novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

:build
set FFMPEG=..\lib-build\ffmpeg-static-win
cl /nologo /EHsc /O2 /MT /W1 /DSTANDALONE /I ..\src /I "%FFMPEG%\include" encoder-compare.cpp ..\src\FFRecorder.cpp ^
  /Fe:encoder-compare.exe ^
  /link /LIBPATH:"%FFMPEG%\lib" libavformat.a libavcodec.a libswscale.a libavutil.a srt_static.lib ^
  mfplat.lib mfuuid.lib ole32.lib strmiids.lib user32.lib bcrypt.lib secur32.lib ws2_32.lib crypt32.lib advapi32.lib
if errorlevel 1 goto failed
del encoder-compare.obj FFRecorder.obj 2>nul
echo.
echo Built %~dp0encoder-compare.exe
goto done

:novs
echo Visual Studio C++ build tools not found.
goto failed

:failed
echo Build failed.

:done
pause
