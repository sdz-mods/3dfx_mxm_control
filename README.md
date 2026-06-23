# 3dfx MXM Control

Native Win32 control panel for the M3800/M4800 MXM cards.

## Build

From PowerShell:

```powershell
$env:PATH = "C:\msys64\mingw32\bin;C:\msys64\usr\bin;$env:PATH"
mingw32-make
```

The output is:

```text
dist/3dfx_mxm_control.exe
```

The application is a single executable. Windows XP uses the built-in NT PCI
config path, while Windows 98 uses direct user-mode I/O.

## License

GPL-2.0-or-later.
