@echo off
call "%~dp0\th07vars.bat"
%*
if %errorlevel% neq 0 exit /b %errorlevel%
