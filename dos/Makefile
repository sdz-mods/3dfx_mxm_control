WATCOM = C:\WATCOM
CC = $(WATCOM)\binnt\wcl.exe
ASM = $(WATCOM)\binnt\wasm.exe
CFLAGS = -q -bt=dos -ms -3 -os -w4 -I=src;..\common
TARGET = ..\dist\3dfxmxm.exe
OBJECTS = ..\build\dos\ui.obj ..\build\dos\pci.obj &
	  ..\build\dos\mxm.obj ..\build\dos\clock.obj &
	  ..\build\dos\mmio.obj

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@cmd /c if not exist ..\dist mkdir ..\dist
	$(CC) -q -bt=dos -ms -3 -fe=$(TARGET) $(OBJECTS)

..\build\dos\ui.obj: src\ui.c
	@cmd /c if not exist ..\build\dos mkdir ..\build\dos
	$(CC) $(CFLAGS) -c -fo=$@ $<

..\build\dos\pci.obj: src\pci.c
	@cmd /c if not exist ..\build\dos mkdir ..\build\dos
	$(CC) $(CFLAGS) -c -fo=$@ $<

..\build\dos\mxm.obj: src\mxm.c
	@cmd /c if not exist ..\build\dos mkdir ..\build\dos
	$(CC) $(CFLAGS) -c -fo=$@ $<

..\build\dos\clock.obj: src\clock.c
	@cmd /c if not exist ..\build\dos mkdir ..\build\dos
	$(CC) $(CFLAGS) -c -fo=$@ $<

..\build\dos\mmio.obj: src\mmio.c
	@cmd /c if not exist ..\build\dos mkdir ..\build\dos
	$(CC) $(CFLAGS) -c -fo=$@ $<

clean: .SYMBOLIC
	@cmd /c if exist ..\build\dos rmdir /s /q ..\build\dos
	@cmd /c if exist $(TARGET) del $(TARGET)
