AS         = $(CROSS_COMPILE)as
LD         = $(CROSS_COMPILE)ld
CC         = $(CROSS_COMPILE)gcc
CPP        = $(CC) -E
AR         = $(CROSS_COMPILE)ar
RANLIB     = $(CROSS_COMPILE)ranlib
NM         = $(CROSS_COMPILE)nm
STRIP      = $(CROSS_COMPILE)strip
OBJCOPY    = $(CROSS_COMPILE)objcopy
OBJDUMP    = $(CROSS_COMPILE)objdump
SIZEUTIL   = $(CROSS_COMPILE)size

MSGFMT     = msgfmt
MSGMERGE   = msgmerge

# Allow git to be wrappered in the environment
GIT        ?= git

INSTALL      = install
INSTALL_DIR  = $(INSTALL) -d -m0755 -p
INSTALL_DATA = $(INSTALL) -m0644 -p
INSTALL_PROG = $(INSTALL) -m0755 -p

PREFIX ?= /usr
BINDIR = $(PREFIX)/bin
INCLUDEDIR = $(PREFIX)/include
LIBLEAFDIR = lib
LIBLEAFDIR_x86_64 = lib64
LIBDIR = $(PREFIX)/$(LIBLEAFDIR)
LIBDIR_x86_64 = $(PREFIX)/$(LIBLEAFDIR_x86_64)
MANDIR = $(PREFIX)/share/man
MAN1DIR = $(MANDIR)/man1
MAN8DIR = $(MANDIR)/man8
SBINDIR = $(PREFIX)/sbin

PRIVATE_PREFIX = $(LIBDIR)/xen
PRIVATE_BINDIR = $(PRIVATE_PREFIX)/bin

SOCKET_LIBS =
CURSES_LIBS = -lncurses
PTHREAD_LIBS = -lpthread
UTIL_LIBS = -lutil
SONAME_LDFLAG = -soname
SHLIB_CFLAGS = -shared

ifneq ($(debug),y)
CFLAGS += -O2 -fomit-frame-pointer
else
# Less than -O1 produces bad code and large stack frames
CFLAGS += -O1 -fno-omit-frame-pointer -fno-optimize-sibling-calls
endif
