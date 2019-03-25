.PHONY: all clean distclean

TZE       = tze
VER_FILE := tze_version.h
HEADERS   = $(wildcard *.h)
OBJECTS   = $(patsubst %.c,%.o,$(sort $(wildcard *.c)))
VERSION  := $(shell git describe --tag 2> /dev/null | sed -e 's/-g/-/')
SVERSION := $(shell sed -e 's/.*"\(.*\)".*/\1/p;d' $(VER_FILE) 2> /dev/null)

CPPFLAGS ?=
LDFLAGS  ?=
CFLAGS   ?= -g3 -pipe

CPPFLAGS += -D_LARGEFILE_SOURCE \
            -D_LARGEFILE64_SOURCE \
            -D_FILE_OFFSET_BITS=64 \
            -D_POSIX_C_SOURCE=200809L \
            -D_BSD_SOURCE=1 \
            -D_XOPEN_SOURCE=600 \
            -D_DEFAULT_SOURCE
CFLAGS   += -std=c99 \
            -ffunction-sections \
            -fdata-sections \
            -fstack-protector-all \
            -ftabstop=4 \
            -Waddress \
            -Wall \
            -Wconversion \
            -Wempty-body \
            -Winit-self \
            -Wmissing-field-initializers \
            -Wpointer-arith \
            -Wredundant-decls \
            -Wshadow \
            -Wstack-protector \
            -Wswitch-enum \
            -Wtype-limits \
            -Wundef \
            -Wvla
LDFLAGS  +=

all: $(TZE)

.PHONY: $(if $(filter $(VERSION),$(SVERSION)),,$(VER_FILE))

$(VER_FILE):
	@echo "#ifndef TZE_VERSION_H"                 > $@
	@echo "#define TZE_VERSION_H"                >> $@
	@echo ""                                     >> $@
	@echo "#define TZE_VERSION \""$(VERSION)"\"" >> $@
	@echo ""                                     >> $@
	@echo "#endif /* TZE_VERSION_H */"           >> $@

tze.o: $(VER_FILE)

$(TZE): $(OBJECTS) $(HEADERS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -f *.o $(TZE) $(VER_FILE)

distclean: clean
