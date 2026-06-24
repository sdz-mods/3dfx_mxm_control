# 3dfx MXM Control

Native Windows 98/XP and DOS control panels for the M3800/M4800 MXM cards.

## Repository layout

```text
common/  Shared card and protocol definitions
dos/     16-bit real-mode DOS application
win32/   Native Windows 98/XP application
```

## Build Win32

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

## Build DOS

Install Open Watcom V2 in `C:\WATCOM`, then run:

```powershell
cmd /c "call C:\WATCOM\owsetenv.bat && cd dos && wmake"
```

The output is:

```text
dist/3dfxmxm.exe
```

This is a single 16-bit real-mode DOS executable and does not require a DOS
extender. See [dos/README.md](dos/README.md) for controls and clock-access
requirements.

## License

GPL-2.0-or-later.
