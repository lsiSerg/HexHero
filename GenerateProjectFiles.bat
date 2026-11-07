@echo off
setlocal

set "UBT=D:\Work\lsiSerg\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe"

"%UBT%" -projectfiles -project="%~dp0HexHero.uproject" -game -rocket -progress
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
	echo Project files generated.
) else (
	echo Failed with exit code %RESULT%.
)

pause
exit /b %RESULT%
