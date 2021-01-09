include RMCIOS-build-scripts/utilities.mk

SOURCES:=serial_channels.c
FILENAME?=windows-serial-module
CFLAGS+= -lwinmm
CFLAGS+= -mwindows
GCC?=${TOOL_PREFIX}gcc
MAKE?=make
export

compile:
	$(MAKE) -f RMCIOS-build-scripts${/}module_dll.mk compile TOOL_PREFIX=${TOOL_PREFIX}

