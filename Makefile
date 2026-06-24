.PHONY: all win32 clean

all: win32

win32:
	$(MAKE) -C win32

clean:
	$(MAKE) -C win32 clean
