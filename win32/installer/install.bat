@echo off
echo Installing 3dfx MXM Control...
if not exist C:\3dfx\nul mkdir C:\3dfx
copy 3dfx_mxm_control.exe C:\3dfx\3dfx_mxm_control.exe
if not exist C:\3dfx\3dfx_mxm_control.exe goto failed
regedit /s autostart.reg
echo.
echo Done. Installed to C:\3dfx; it starts in the system tray at next logon.
echo To start it now:  C:\3dfx\3dfx_mxm_control.exe /tray
goto end
:failed
echo.
echo ERROR: could not copy 3dfx_mxm_control.exe to C:\3dfx.
echo Run install.bat from the folder that also contains the .exe.
:end
pause