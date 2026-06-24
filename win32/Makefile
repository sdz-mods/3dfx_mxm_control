CC := C:/msys64/mingw32/bin/gcc.exe
WINDRES := C:/msys64/mingw32/bin/windres.exe
SHELL := C:/msys64/usr/bin/sh.exe

TARGET := ../dist/3dfx_mxm_control.exe
OBJS := ../build/win32/app.o ../build/win32/io_backend.o \
	../build/win32/pci.o ../build/win32/mxm.o \
	../build/win32/hwc_ext.o ../build/win32/resource.o

CFLAGS := -std=gnu99 -Os -Wall -Wextra -Wno-unused-parameter \
	-DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0400 \
	-Isrc -I../common
LDFLAGS := -mwindows -Wl,--subsystem,windows:4.0 -static-libgcc
LIBS := -lcomctl32 -lshell32 -ladvapi32 -lgdi32

.PHONY: all clean

all: $(TARGET)

../build/win32:
	mkdir -p ../build/win32 ../dist

$(TARGET): $(OBJS) | ../build/win32
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

../build/win32/%.o: src/%.c | ../build/win32
	$(CC) $(CFLAGS) -c $< -o $@

../build/win32/resource.o: src/resource.rc src/resource.h src/3dfx_logo.ico | ../build/win32
	$(WINDRES) -Isrc src/resource.rc -O coff -o $@

clean:
	rm -rf ../build/win32 $(TARGET)
