@echo off
setlocal
rem Usage: build_fbneo_checkpoint.cmd "E:\path\to\salvia-toolchain"
rem Mirror the reviewed Git source paths to TC\.work\Salvia before invoking.
if "%~1"=="" goto :usage
for %%I in ("%~1") do set "TC=%%~fI"
set "SRC=%TC%\.work\Salvia"
set "LOG=%TC%\.work\build-fbneo-checkpoint.log"
if not exist "%TC%\scripts\msbuild.cmd" goto :usage
if not exist "%SRC%\Salvia.vcxproj" goto :usage
echo Reversible FBNeo checkpoint build > "%LOG%"
echo Building FBNeo core
call "%TC%\scripts\msbuild.cmd" "%SRC%\libretro\FBNeo\projectfiles\visualstudio-2010-libretro-360\fba_vs2010_libretro_360.sln" /p:Configuration=Release /t:Rebuild /v:minimal /p:PostBuildEventUseInBuild=false >> "%LOG%" 2>&1
if errorlevel 1 goto :failed
echo FBNeo core PASS
call "%TC%\scripts\msbuild.cmd" "%SRC%\libs\libSDLx360\libSDLx360.vcxproj" /p:Configuration=Release /t:Build /v:minimal /p:PostBuildEventUseInBuild=false >> "%LOG%" 2>&1
if errorlevel 1 goto :failed
echo SDL PASS
call "%TC%\scripts\msbuild.cmd" "%SRC%\Salvia.vcxproj" /p:Configuration=Release_finalburn /t:Rebuild /v:minimal /p:PostBuildEventUseInBuild=false >> "%LOG%" 2>&1
if errorlevel 1 goto :failed
echo Salvia frontend PASS
echo Verify post-image XEX copies, embedded build ID and source hashes before delivery.
exit /b 0
:failed
echo FAILED. See "%LOG%"
exit /b 1
:usage
echo Specify the existing toolchain root containing scripts\msbuild.cmd and .work\Salvia.
exit /b 2
