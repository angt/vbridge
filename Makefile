LDFLAGS := -Wl,-O1,--sort-common,--as-needed

ALL_CFLAGS  := -std=c99 -ffreestanding -Wall -Wextra $(CFLAGS)
ALL_LDFLAGS := $(LDFLAGS)

QUIET = @echo '$@';

CC      ?= gcc
STRIP   ?= strip
INSTALL ?= install

prefix  := /usr

ifeq ($(temps),yes)
ALL_CFLAGS += -save-temps=obj
endif

mode := release
arch := $(shell $(CC) -dumpmachine | cut -f1 -d-)

ifeq ($(arch),x86_64)
ALL_CFLAGS += -mtune=generic -m64 -march=x86-64
else
override arch := i686
ALL_CFLAGS += -mtune=generic -m32 -march=i686
endif

mark := $(mode)-$(arch)-

ifeq ($(mode),release)
ALL_CFLAGS += -O3 -fomit-frame-pointer
endif

ifeq ($(mode),debug)
ALL_CFLAGS += -O0 -g -fno-omit-frame-pointer -fsanitize=address
else
ALL_CFLAGS += -DNDEBUG
endif

ifeq ($(mode),coverage)
ALL_CFLAGS += -O0 -g -fno-omit-frame-pointer -fprofile-arcs -ftest-coverage
LDLIBS += -lgcov
endif

ifeq ($(mode),profile)
ALL_CFLAGS += -O0 -g -pg -fno-omit-frame-pointer
endif

ifeq ($(mode),pgo)
ifneq ($(pgo),use)
ALL_CFLAGS += -O0
endif
ALL_CFLAGS += -fprofile-$(pgo)
endif

-include config.mk

SERVER   := $(PROG_SERVICE)d
CLIENT   := $(PROG_SERVICE)
SOURCES  := $(wildcard *.c)
TESTS    := $(basename $(filter test-%,$(SOURCES)))
DOTINS   := $(basename $(wildcard *.in))
DISTNAME := $(PROG_SERVICE)-$(PROG_VERSION)-$(mode)-$(arch)

ifeq ($(findstring mingw,$(CC)),mingw)
MN_$(CLIENT).exe := client-win
LD_client-win.o  := -lws2_32 -lgdi32
PROGRAMS := $(CLIENT).exe
DEFAULT  := $(CLIENT).exe
DIST     := zip
else
MN_$(SERVER)       := server
MN_$(CLIENT)       := client
LD_screen.o        := -lxcb -lxcb-xfixes -lxcb-shm -lxcb-randr -lxcb-damage
LD_common.o        := -lrt
LD_user.o          := -lcap
LD_display.o       := -lX11 -lXfixes
LD_image.o         := -lXext
LD_input.o         := -lXi -lXtst
LD_xrandr.o        := -lXrandr
LD_xdamage.o       := -lXdamage
LD_auth-pam.o      := -lpam
LD_auth-gss.o      := -lgssapi_krb5 -lkrb5
LD_client.o        := -lXrender
PROGRAMS := $(SERVER) $(CLIENT) $(TESTS)
DEFAULT  := $(SERVER) $(CLIENT)
DIST     := tar.gz
endif

LD_openssl.o := -lssl -lcrypto

.SUFFIXES:
.PHONY: default all install dist test clean

default: $(DEFAULT)

all: $(PROGRAMS)

install: $(CLIENT) $(SERVER)
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -s $(SERVER) $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -s $(CLIENT) $(DESTDIR)$(prefix)/bin

dist: $(DISTNAME).$(DIST)

test: test-tycho
	./test-tycho -c 50 $(data) 2> test-tycho.dat

clean:
	@rm -f $(PROGRAMS) $(DOTINS) *.[dios] *.mk

ucs_to_keysym-static.h: ucs_to_keysym.awk keysymdef.h
	$(QUIET)awk -f $^ > $@

-include $(mark)deps.mk
-include $(mark)o.mk

$(foreach k,$(PROGRAMS),\
    $(eval $(k): $(addprefix $(mark),$(filter $(SOURCES:.c=.o),$(O_$(mark)$(k).o) $(O_$(mark)$(MN_$(k)).o)))))

$(PROGRAMS):
	$(QUIET)$(CC) $(ALL_CFLAGS) $(CPPFLAGS) $(ALL_LDFLAGS) $^ -o $@ $(LDLIBS) $(foreach k,$^,$(LD_$(k:$(mark)%=%)))

$(DOTINS): %: %.in
	$(QUIET)sed -n 's/^\([A-Z_]*\)\s\+:=\s\+\(.*\)$$/-e "s^@@\1@@^\2^g"/p' config.mk | xargs sed $< > $@

$(DISTNAME): $(DEFAULT)
	@$(STRIP) -s -R .comment -R .note -R .eh_frame_hdr -R .eh_frame $^ && mkdir -p $@; cp -rf $^ -t $@

$(mark)deps.mk: $(SOURCES) $(wildcard *.h)
	@$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -MM $(SOURCES) | sed 's/^\(.*:.*\)$$/$(mark)\1/g' > $@

$(mark)o.mk: $(mark)deps.mk
	@sed -e 's/\.[hc]/\.o/g' -e 's/^\(.*\):/O_\1:=/g' $< > $@

config.mk: config.h
	@sed -n "s/^#define\s\+\(PROG_[A-Z_]\+\)\s\+\"*\([^\"]*\)\"*.*$$/\1 := \2/p" $< > $@

$(mark)%.o: %.c
	$(QUIET)$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -c $< -o $@

%.tar.gz: %
	$(QUIET)tar -czf $@ $< --remove-files

%.zip: %
	$(QUIET)zip -qrm $@ $<
