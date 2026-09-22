@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
cl /EHsc /O2 /nologo /MT "%~dp0resolve.cpp" /link /nologo dbghelp.lib /OUT:"%~dp0resolve.exe" || exit /b 1
