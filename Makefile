PROG_SERVICE := $(shell sed -n 's|.*define.*PROG_SERVICE.*"\(.*\)".*|\1|p' src/config.h)
PROG_VERSION := $(shell ./version.sh)

CPPFLAGS := -DPROG_VERSION=\"$(PROG_VERSION)\"
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

SERVER   := $(PROG_SERVICE)d
CLIENT   := $(PROG_SERVICE)
SOURCES  := $(wildcard src/*.c)
HEADERS  := $(wildcard src/*.h)
DISTNAME := $(PROG_SERVICE)-$(PROG_VERSION)-$(mode)-$(arch)

ifeq ($(findstring mingw,$(CC)),mingw)
MN_$(CLIENT).exe := client-win
LD_client-win.o  := -lws2_32 -lgdi32
PROGRAMS := $(CLIENT).exe
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
PROGRAMS := $(SERVER) $(CLIENT)
DIST     := tar.gz
endif

LD_openssl.o := -lssl -lcrypto

.SUFFIXES:
.PHONY: default all install dist test

default: $(PROGRAMS)

all: defaults test

install: $(CLIENT) $(SERVER)
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -s $(SERVER) $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -s $(CLIENT) $(DESTDIR)$(prefix)/bin

dist: $(DISTNAME).$(DIST)

test: $(patsubst src/test-%.c, src/$(mark)test-%.bin, $(SOURCES))

-include src/$(mark)h.mk
-include src/$(mark)o.mk

$(foreach k,$(PROGRAMS),$(eval $(k): src/$(mark)$(MN_$(k)).bin))

$(PROGRAMS):
	$(QUIET)cp $< $@

$(DISTNAME): $(PROGRAMS)
	@$(STRIP) -s -R .comment -R .note -R .eh_frame_hdr -R .eh_frame $^ && mkdir -p $@; cp -rf $^ -t $@

src/ucs_to_keysym-static.h: src/ucs_to_keysym.awk src/keysymdef.h
	$(QUIET)awk -f $^ > $@

src/$(mark)h.mk: $(SOURCES) $(HEADERS)
	@$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -MM $(SOURCES) | sed 's|^\(.*:.*\)$$|src/$(mark)\1|g' > $@

src/$(mark)o.mk: $(patsubst src/%.c, src/$(mark)%.o, $(SOURCES))
	@for f in $^; do grep -sq '^main$$' "$$f.def" || continue ;\
	    U="/tmp/$$(basename "$$f").$$$$.und" ;\
	    cp "$$f.und" "$$U" ;\
	    while true; do \
	        (cat "$$U"; fgrep -w -l -f "$$U" src/*.o.def | sed 's/.def/.und/' | xargs cat) 2>/dev/null | sort -u > "$$U".2 ;\
	        cmp -s "$$U" "$$U".2 && break ;\
	        mv "$$U".2 "$$U" ;\
	    done ;\
	    echo $${f%%.o}.bin: $$f $$(fgrep -w -l -f "$$U" src/*.o.def | sed 's/.def//') ;\
	    rm -f "$$U"* ;\
	done > $@

src/$(mark)%.bin:
	$(QUIET)$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $^ -o $@ $(LDLIBS) $(foreach k,$^,$(LD_$(k:src/$(mark)%=%)))

src/$(mark)%.o: src/%.c
	$(QUIET)$(CC) $(ALL_CFLAGS) $(CPPFLAGS) -c $< -o $@ && objdump -t $@ | awk '/\*UND\*/{print $$NF >> "$@.und"} /g.*F.*\.text/{print $$NF >> "$@.def"}'

%.tar.gz: %
	$(QUIET)tar -czf $@ $< --remove-files

%.zip: %
	$(QUIET)zip -qrm $@ $<
