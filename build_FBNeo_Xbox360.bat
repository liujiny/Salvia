@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: =======================================================
:: 1. 检测并加载 VS2010 环境变量 (Environment Variables)
:: =======================================================
for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Microsoft\VisualStudio\SxS\VS7" /v "10.0" 2^>nul ^| find "10.0"') do set "VS100COMNTOOLS=%%b"
if not defined VS100COMNTOOLS for /f "tokens=2*" %%a in ('reg query "HKLM\SOFTWARE\Wow6432Node\Microsoft\VisualStudio\SxS\VS7" /v "10.0" 2^>nul ^| find "10.0"') do set "VS100COMNTOOLS=%%b"
if not defined VS100COMNTOOLS (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\vcvars32.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio 10.0\VC\bin\vcvars32.bat"
    ) else (
        echo ERROR: 未找到 VS2010 安装路径 (VS2010 not found)
        pause
        exit /b 1
    )
) else (
    call "%VS100COMNTOOLS%\..\..\VC\bin\vcvars32.bat"
)

:: =======================================================
:: 2. 配置通用编译参数 (Build Configuration)
:: =======================================================
set PLATFORM="Xbox 360"
set CONFIG=Release
set MS_OPTS=/t:Rebuild /p:Platform=%PLATFORM% /v:q /nologo /clp:NoSummary /p:DeployOnBuild=false

if not exist Distro360 md Distro360

:: =======================================================
:: 3. 编译公共依赖库 (Compile Common Libraries)
:: =======================================================
echo =======================================================
echo [1/4] 正在编译公共底层库 (Common Libraries)...
echo =======================================================

set LIBS=libs\SDL_image-1.2.12\libjpeg\jpeg.vcxproj ^
libs\SDL_image-1.2.12\lpng1513\projects\vstudio\libpng\libpng.vcxproj ^
libs\SDL_image-1.2.12\VisualC\SDL_image.vcxproj ^
libs\SDL_ttf360\SDL_ttf360.vcxproj ^
libs\libSDLx360\libSDLx360.vcxproj ^
libs\wolfssl\IDE\XBOX360\wolfssl.vcxproj ^
libs\curl\projects\Windows\VC10\lib\libcurl.vcxproj ^
libs\minizip\minizip.vcxproj ^
libs\zlib\zlib.vcxproj ^
libs\rcheevos\rcheevos.vcxproj

for %%C in (%LIBS%) do (
    echo [+] 正在构建库: %%C...
    msbuild "%%C" %MS_OPTS% /p:Configuration=%CONFIG% > nul
    if errorlevel 1 (
        echo.
        echo [X] 编译失败: %%C. 正在重试并输出详细错误日志:
        msbuild "%%C" %MS_OPTS% /p:Configuration=%CONFIG% /v:m /nologo
        goto :error
    )
)

:: =======================================================
:: 4. 单独编译 FBNeo 核心 (Compile FBNeo Core)
:: =======================================================
echo.
echo =======================================================
echo [2/4] 正在编译 FBNeo 核心库 (FBNeo Libretro Core)...
echo =======================================================

set FBNEO_SLN=libretro\FBNeo\projectfiles\visualstudio-2010-libretro-360\fba_vs2010_libretro_360.sln

echo [+] 正在构建核心: %FBNEO_SLN%...
msbuild "%FBNEO_SLN%" /t:Rebuild /p:Platform=%PLATFORM% /p:Configuration=%CONFIG% /v:q /nologo /clp:NoSummary > nul
if errorlevel 1 (
    echo.
    echo [X] FBNeo 核心编译失败. 正在重试并输出详细错误日志:
    msbuild "%FBNEO_SLN%" /t:Build /p:Platform=%PLATFORM% /p:Configuration=%CONFIG% /v:m /nologo
    goto :error
)

:: =======================================================
:: 5. 链接生成 FBNeo 版 Salvia 前端 (Link Frontend Executable)
:: =======================================================
echo.
echo =======================================================
echo [3/4] 正在链接并生成 Salvia FBNeo 可执行程序 (.xex)...
echo =======================================================

set PROJECT=Salvia.vcxproj
set CONFIG_NAME=Release_finalburn

echo [+] 正在集成并链接: %CONFIG_NAME%...
msbuild "%PROJECT%" %MS_OPTS% /p:Configuration=%CONFIG_NAME% > nul
if errorlevel 1 (
    echo.
    echo [X] 前端链接失败: %CONFIG_NAME%. 正在重试并输出详细错误日志:
    msbuild "%PROJECT%" /t:Build /p:Platform=%PLATFORM% /p:Configuration=%CONFIG_NAME% /v:m /nologo
    goto :error
)

:: =======================================================
:: 6. 编译插件 (Compile Plugins)
:: =======================================================
echo.
echo =======================================================
echo [4/4] 正在编译插件 (Compiling Plugins)...
echo =======================================================

echo [+] 正在构建插件: hidmouse...
msbuild "plugins\hidmouse\hidmouse.vcxproj" %MS_OPTS% /p:Configuration=%CONFIG% > nul
if errorlevel 1 (
    echo.
    echo [X] 插件编译失败: hidmouse. 正在重试并输出详细错误日志:
    msbuild "plugins\hidmouse\hidmouse.vcxproj" /t:Build /p:Platform=%PLATFORM% /p:Configuration=%CONFIG% /v:m /nologo
    goto :error
)

:: =======================================================
:: 完成退出
:: =======================================================
echo.
echo =======================================================
echo [OK] FBNeo 专属版本构建完成！(Build Succeeded)
echo 输出文件已保存至 Distro360 目录。
echo =======================================================
pause
exit /b 0

:error
echo.
echo [!] 编译中止 (Build Aborted).
pause
exit /b 1