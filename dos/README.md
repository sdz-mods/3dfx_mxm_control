# DOS application

`3dfxmxm.exe` is a single-file, 16-bit real-mode DOS control panel for the
M3800 and M4800 cards.

## Controls

- Up/Down: select a setting
- Left/Right: change the selected value
- F5: refresh card information and settings
- F9: load safe defaults
- F10: apply settings
- F1: display help
- Esc: exit

Backlight, voltage, framebuffer size, and blank-fix settings use the card's
hardware control interface, as in the Win32 application.

Clock control uses direct MMIO and requires pure real mode. It is disabled
when the application detects a V86 monitor such as EMM386, because changing
protected-mode state from V86 mode is unsafe.

The application uses 386 instructions. This is only a general compatibility
note; all intended M3800/M4800 host systems exceed that requirement.

## Build

Install Open Watcom V2 in `C:\WATCOM`, then run:

```bat
cd dos
call C:\WATCOM\owsetenv.bat
wmake
```

The output is `dist\3dfxmxm.exe`.
