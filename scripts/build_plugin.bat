@echo off
REM Build UE5UltimateMCP as a standalone plugin binary
REM This creates a pre-compiled version that works in Blueprint-only projects
REM
REM Usage: build_plugin.bat [engine_path] [output_path]
REM   engine_path  - Path to UE5 install (default: auto-detect from Epic launcher)
REM   output_path  - Where to put the built plugin (default: .\BuiltPlugin)

setlocal

set ENGINE_PATH=%~1
set OUTPUT_PATH=%~2

if "%OUTPUT_PATH%"=="" set OUTPUT_PATH=%~dp0..\BuiltPlugin

REM Auto-detect engine if not provided
if "%ENGINE_PATH%"=="" (
    REM Try common locations
    if exist "E:\EpicGames\Games\UE_5.7\Engine" (
        set ENGINE_PATH=E:\EpicGames\Games\UE_5.7
    ) else if exist "C:\Program Files\Epic Games\UE_5.7\Engine" (
        set ENGINE_PATH=C:\Program Files\Epic Games\UE_5.7
    ) else (
        echo ERROR: Could not find UE5.7. Pass the engine path as first argument.
        echo Example: build_plugin.bat "E:\EpicGames\Games\UE_5.7"
        exit /b 1
    )
)

set UAT=%ENGINE_PATH%\Engine\Build\BatchFiles\RunUAT.bat
set PLUGIN_PATH=%~dp0..\UE5UltimateMCP.uplugin

if not exist "%UAT%" (
    echo ERROR: RunUAT.bat not found at %UAT%
    exit /b 1
)

echo.
echo ========================================
echo  Building UE5 Ultimate MCP Plugin
echo ========================================
echo  Engine: %ENGINE_PATH%
echo  Plugin: %PLUGIN_PATH%
echo  Output: %OUTPUT_PATH%
echo ========================================
echo.

call "%UAT%" BuildPlugin -Plugin="%PLUGIN_PATH%" -Package="%OUTPUT_PATH%" -TargetPlatforms=Win64 -Rocket

if %ERRORLEVEL% neq 0 (
    echo.
    echo BUILD FAILED. Check the output above for errors.
    exit /b 1
)

echo.
echo ========================================
echo  BUILD SUCCESSFUL
echo ========================================
echo.
echo Plugin built to: %OUTPUT_PATH%
echo.
echo To use it:
echo   1. Copy %OUTPUT_PATH% into YourProject\Plugins\UE5UltimateMCP\
echo   2. Open UE5 -- plugin loads automatically
echo   3. Run: claude mcp add ue5 -- node Plugins/UE5UltimateMCP/MCP/dist/index.js
echo   4. Start claude in your project folder
echo.
