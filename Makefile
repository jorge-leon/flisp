#
# fLisp Makefile, leg20251226
#

AR      = ar
CC      = cc
CPP     = cpp
#CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE -DNDEBUG
CPPFLAGS += -D_DEFAULT_SOURCE -D_BSD_SOURCE
#CFLAGS += -O2 -std=c11 -Wall -pedantic -pedantic-errors
CFLAGS += -O0 -std=c11 -Wall -pedantic -pedantic-errors -Werror=format-security -Wformat -g
LD      = cc
#LDFLAGS = --static
LDFLAGS =
LIBS    =
CP      = cp
MV      = mv
RM      = rm
MKDIR	= mkdir
PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
LIBDIR  = $(PREFIX)/lib
INCDIR  = $(PREFIX)/include
DATADIR = $(PREFIX)/share
DOCDIR  = $(DATADIR)/doc
PACKAGE = flisp

OBJ = double.o lisp.o posix.o string.o
BINARIES = fl
SCRIPTS = flisp
LIBRARIES = libflisp.a
RC_FILES = init.lsp
HEADER = lisp.h string.h posix.h double.h

LISPLIB = flisp.lsp string.lsp file.lsp cl.lsp
SOURCES = fl.c flisp.sht lisp.c lisp.h double.c double.h posix.c posix.h string.c string.h

DOCFILES = README.md doc/flisp.html doc/develop.html doc/history.html doc/implementation.html
MOREDOCS = README.html doc/flisp.md doc/develop.md doc/history.md doc/implementation.md

.SUFFIXES: .lsp .sht  .md .html
.sht.lsp:
	./sht $*.sht >$@

all: $(BINARIES) $(SCRIPTS) $(LIBRARIES) flisp.pc

debug: CPPFLAGS += -UNDEBUG -g
debug: $(BINARIES) $(LIBRARIES)

double.o: double.c double.h lisp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

fl: fl.o lisp.o $(OBJ)
	$(LD) $(LDFLAGS) -o $@ $^ -lm

flisp: flisp.sht fl core.lsp
	BINDIR=$(BINDIR) FLISPLIB=$(DATADIR)/$(PACKAGE) ./sht $< > $@
	chmod +x $@

fl.o: fl.c lisp.h posix.h double.h string.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

flisp.pc: flisp.pc.sht
	PREFIX=$(PREFIX) ./sht $< > $@

init.lsp: init.sht core.lsp

lisp.o: lisp.c lisp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

libflisp.a: $(OBJ)
	$(AR) rcs $@ $^

posix.o: posix.c posix.h lisp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

string.o: string.c string.h lisp.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<


# Requires pandoc and tidy
doc: $(MOREDOCS)

doc/flisp.md: doc/flisp.html h2m.lua
	tidy -m -config tidyrc $<
	pandoc -o $@ -t gfm -L h2m.lua $<

doc/develop.md: doc/develop.html h2m.lua
	tidy -m -config tidyrc $<
	pandoc -o $@ -t gfm -L h2m.lua $<

doc/history.md: doc/history.html h2m.lua
	tidy -m -config tidyrc $<
	pandoc -o $@ -t gfm -L h2m.lua $<

doc/implementation.md: doc/implementation.html h2m.lua
	tidy -m -config tidyrc $<
	pandoc -o $@ -t gfm -L h2m.lua $<

README.html: README.md
	pandoc -o $@ -f gfm $<

# Requires doxygen and graphviz (dot)
doxygen: FORCE
	doxygen

# Development
fli: fl flisp FORCE
	FLISP_DEBUG=f.log FLISPLIB=. $$(which rlwrap) ./fl flisp
fld: fl FORCE
	FLISP_DEBUG=f.log FLISPLIB=. gdb ./fl
flv: fl FORCE
	FLISP_DEBUG=f.log FLISPLIB=. valgrind ./fl
frama-c: FORCE
	frama-c -c11 -cpp-extra-args="-I$(frama-c -print-path)/libc -I/usr/include -I." -kernel-msg-key pp -metrics *.c

LISPSRC = $(SCRIPTS) $(LISPLIB)
ALLSRC = $(SOURCES) $(LISPSRC)
# Requires sloccount
measure: $(RC_FILES) $(BINARIES) $(SCRIPTS) strip FORCE
	@echo
	@echo fLisp Code Stats
	@echo
	@echo "libsize: " $$(set -- $$(ls -l libflisp.a); echo $$5)
	@echo "binsize: " $$(set -- $$(ls -l fl); echo $$5)
	@echo "              C  Lisp  Total"
	@echo "lines:     $$(cat $(SOURCES) | wc -l)   $$(cat $(LISPSRC) | wc -l)   $$(cat $(ALLSRC) | wc -l)"
	@echo "files:        $$(echo $(SOURCES) | wc -w)     $$(echo $(LISPSRC) | wc -w)     $$(echo $(ALLSRC) | wc -w)"
	@echo "sloccount: " \
	    $$(set -- $$(which sloccount >/dev/null && \
	        { sloccount $(ALLSRC) | grep ansic=; }); echo $$3)	

# Requires splint
splint: FORCE
	splint +posixlib -macrovarprefix "M_" *.c *.h

TAGS: FORCE
	ctags -e *.c *.h *.lsp

test: fl test/test.lsp FORCE
	@(cd test && ./test.sh -as)

# Exit 1 if any testsuite fails
check: fl test/test.lsp FORCE
	@(cd test && ./test.sh -sa | grep tests, | \
	while read RESULT; do \
	   RESULT=$${RESULT#* tests, }; \
	   RESULT=$${RESULT% failures*}; \
	   [ "$$RESULT" = 0 ] || { echo failed >&2; exit 1; } \
        done )

test/test.lsp: test/test.sht core.lsp

# Install/package
strip: $(BINARIES) $(LIBRARIES) FORCE
	strip $(BINARIES) $(LIBRARIES)

clean: FORCE
	-$(RM) -f $(OBJ) $(BINARIES) $(SCRIPTS) $(LIBRARIES) $(RC_FILES) fl.o flisp.pc
	-$(RM) -rf doxygen
	-$(RM) -f $(MOREDOCS)
	-$(RM) -f f.log
	-$(RM) -f test/test.lsp  test/f.log
	-$(RM) -rf debian/flisp debian/flisp-dev debian/flisp-common debian/flisp-doc

deb: FORCE
	dpkg-buildpackage -b -us -uc

# fLisp standalone
install: install-bin install-lib install-doc

install-bin: $(BINARIES) $(SCRIPTS) FORCE
	-$(MKDIR) -p $(DESTDIR)$(BINDIR)
	-$(CP) $(BINARIES) $(SCRIPTS) $(DESTDIR)$(BINDIR)

install-doc: $(DOCFILES) FORCE
	-$(MKDIR) -p $(DESTDIR)$(DOCDIR)/$(PACKAGE)
	-$(CP) $(DOCFILES) $(DESTDIR)$(DOCDIR)/$(PACKAGE)

install-moredocs: $(MOREDOCS) FORCE
	-$(MKDIR) -p $(DESTDIR)$(DOCDIR)/$(PACKAGE)
	-$(CP) $(MOREDOCS) $(DESTDIR)$(DOCDIR)/$(PACKAGE)

install-lib: $(RC_FILES) $(LISPLIB) FORCE
	-$(MKDIR) -p $(DESTDIR)$(DATADIR)/$(PACKAGE)
	-$(CP) $(RC_FILES) $(LISPLIB) $(DESTDIR)$(DATADIR)/$(PACKAGE)

install-dev: $(LIBRARIES) $(HEADER) core.lsp flisp.pc FORCE
	-$(MKDIR) -p $(DESTDIR)$(LIBDIR)
	-$(CP) $(LIBRARIES) $(DESTDIR)$(LIBDIR)
	-$(MKDIR) -p $(DESTDIR)$(INCDIR)/$(PACKAGE)
	-$(CP) $(HEADER) $(DESTDIR)$(INCDIR)/$(PACKAGE)
	-$(MKDIR) -p $(DESTDIR)$(DATADIR)/$(PACKAGE)
	-$(CP) core.lsp $(DESTDIR)$(DATADIR)/$(PACKAGE)
	-$(MKDIR) -p $(DESTDIR)$(LIBDIR)/pkgconfig
	-$(CP) flisp.pc $(DESTDIR)$(LIBDIR)/pkgconfig

uninstall: FORCE
	-(cd  $(DESTDIR)$(BINDIR) && $(RM) -f $(BINARIES) $(SCRIPTS))
	-(cd  $(DESTDIR)$(LIBDIR) && $(RM) -f $(LIBRARIES))
	-(cd  $(DESTDIR)$(LIBDIR)/pkgconfig && $(RM) -f $(PACKAGE).pc)
	-$(RM) -rf $(DESTDIR)$(INCDIR)/$(PACKAGE)
	-$(RM) -rf $(DESTDIR)$(DATADIR)/$(PACKAGE)
	-$(RM) -rf $(DESTDIR)$(DOCDIR)/$(PACKAGE)

# Used as dependency forces rebuild, aka .PHONY in GNU make
FORCE: ;
