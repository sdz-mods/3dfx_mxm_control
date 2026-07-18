@echo off
echo Removing 3dfx MXM Control autostart...
regedit /s uninstall.reg
echo.
echo Autostart removed. Files in C:\3dfx left in place; delete manually if desired.
pause