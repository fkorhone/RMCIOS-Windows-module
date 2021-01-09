include RMCIOS-build-scripts/utilities.mk

SOURCES:=program_channels.c
FILENAME:=windows-program-module
CFLAGS+=-lwinmm
CFLAGS+=-mwindows
CC?=${TOOL_PREFIX}gcc
MAKE?=make
export

compile:
	$(MAKE) -f RMCIOS-build-scripts${/}module_dll.mk compile TOOL_PREFIX=${TOOL_PREFIX}

