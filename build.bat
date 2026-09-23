@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1

set ROOT=%~dp0
set OUT=%ROOT%\dist
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /MT /O2 /GL /W4 /GS- ^
   /Fe:"%OUT%\dxgi.dll" /Fo:"%OUT%\\" /Fd:"%OUT%\\" ^
   "%ROOT%\src\dllmain.cpp" "%ROOT%\src\dxgi_exports.cpp" ^
   "%ROOT%\src\uiconstraint.cpp" "%ROOT%\src\log.cpp" "%ROOT%\src\detour.cpp" "%ROOT%\src\hooks.cpp" ^
   /link /nologo /DLL /OPT:REF /OPT:ICF /LTCG user32.lib kernel32.lib || exit /b 1

echo.
echo Built: %OUT%\dxgi.dll
echo Install: copy %OUT%\dxgi.dll into
echo   E:\SteamLibrary\steamapps\common\Townfall\Townfall\Binaries\Win64\
