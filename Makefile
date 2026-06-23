CC := C:/msys64/mingw32/bin/gcc.exe
WINDRES := C:/msys64/mingw32/bin/windres.exe
SHELL := C:/msys64/usr/bin/sh.exe

TARGET := dist/3dfx_mxm_control.exe
OBJS := build/app.o build/io_backend.o build/pci.o build/mxm.o build/hwc_ext.o build/resource.o

CFLAGS := -std=gnu99 -Os -Wall -Wextra -Wno-unused-parameter \
	-DWINVER=0x0400 -D_WIN32_WINNT=0x0400 -D_WIN32_IE=0x0400 \
	-Isrc
LDFLAGS := -mwindows -Wl,--subsystem,windows:4.0 -static-libgcc
LIBS := -lcomctl32 -lshell32 -ladvapi32 -lgdi32

.PHONY: all clean

all: $(TARGET)

build:
	mkdir -p build dist

$(TARGET): $(OBJS) | build
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/resource.o: src/resource.rc src/resource.h src/3dfx_logo.ico | build
	$(WINDRES) -Isrc src/resource.rc -O coff -o $@

clean:
	rm -rf build $(TARGET)
