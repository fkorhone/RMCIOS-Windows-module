include RMCIOS-build-scripts/utilities.mk

TOOL_PREFIX?=
GCC?=${TOOL_PREFIX}gcc
DLLTOOL?=${TOOL_PREFIX}dlltool
MAKE?=make
INSTALLDIR:=..${/}..
export

all: windows-module windows-gui-module windows-pipe-module windows-program-module windows-serial-module windows-socket-module

windows-module:
	$(MAKE) -f windows-module.mk

windows-gui-module:
	$(MAKE) -f windows-gui-module.mk

windows-pipe-module:
	$(MAKE) -f windows-pipe-module.mk

windows-program-module:
	$(MAKE) -f windows-program-module.mk

windows-serial-module:
	$(MAKE) -f windows-serial-module.mk

windows-socket-module:
	$(MAKE) -f windows-socket-module.mk

install:
	-${MKDIR} "${INSTALLDIR}${/}modules"
	${COPY} *.dll ${INSTALLDIR}${/}modules

