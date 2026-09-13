@echo off
call "D:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\vcvars32.bat"
if errorlevel 1 exit /b 1
cl /nologo /TC /W3 tools\raiden2_stage3_verify.c /Fobuild\raiden2_stage3_verify.obj /Febuild\raiden2_stage3_verify.exe
if errorlevel 1 exit /b 1
build\raiden2_stage3_verify.exe
