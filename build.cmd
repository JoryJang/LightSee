@echo off
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD=%%i
"%MSBUILD%" "%~dp0QtWidgetsLight.sln" /t:Build /p:Configuration=%1 /p:Platform=x64 /m:1 /nologo /v:m
exit /b %ERRORLEVEL%
