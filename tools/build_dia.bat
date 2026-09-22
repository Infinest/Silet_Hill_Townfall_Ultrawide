@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
set DIASDK=C:\Program Files\Microsoft Visual Studio\18\Enterprise\DIA SDK
cl /EHsc /O2 /nologo /MT "%~dp0dia_layout.cpp" /I"%DIASDK%\include" /link /nologo "%DIASDK%\lib\amd64\diaguids.lib" ole32.lib oleaut32.lib /OUT:"%~dp0dia_layout.exe" || exit /b 1
