include RMCIOS-build-scripts/utilities.mk

SOURCES:=socket_channels.c
FILENAME?=windows-socket-module
CFLAGS+=-lwinmm
CFLAGS+=-mwindows
CFLAGS+=-lws2_32
GCC?=${TOOL_PREFIX}gcc
MAKE?=make
export

compile:
	$(MAKE) -f RMCIOS-build-scripts${/}module_dll.mk compile TOOL_PREFIX=${TOOL_PREFIX}

