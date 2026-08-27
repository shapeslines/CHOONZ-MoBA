@echo off
rem Compatibility entrypoint for the pre-push hook and direct use from any shell.
rem The PowerShell runner owns Visual Studio discovery, unconditional ci configure,
rem /WX build, CTest, and the ignored local evidence report.
setlocal

set "ROOT=%~dp0..\.."
pushd "%ROOT%" || exit /b 1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\local-ci.ps1" -Configuration Debug
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
