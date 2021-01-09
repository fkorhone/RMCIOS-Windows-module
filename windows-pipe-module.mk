include RMCIOS-build-scripts/utilities.mk

SOURCES:=pipeserver.c
FILENAME?=windows-pipe-module
CFLAGS+=-lwinmm
CFLAGS+=-mwindows
CC?=${TOOL_PREFIX}gcc
MAKE?=make
export

compile:
	$(MAKE) -f RMCIOS-build-scripts${/}module_dll.mk compile TOOL_PREFIX=${TOOL_PREFIX} 
