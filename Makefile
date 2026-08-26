# SPDX-License-Identifier: GPL-3.0-or-later
CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
LIBDIR ?= $(PREFIX)/lib
BUILD_DIR ?= build
VERSION := 0.1.0-alpha.1

BASE_CPPFLAGS = -D_FORTIFY_SOURCE=3 -DSYNAPSE_DOC_VERSION='"$(VERSION)"'
BASE_CFLAGS = -O2 -g -std=c17 -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong -fPIE -march=x86-64 -mtune=generic
BASE_LDFLAGS = -Wl,-z,relro,-z,now -pie
PKGS = json-c libcrypto ncursesw

CORE_ROOT ?=
ifeq ($(strip $(CORE_ROOT)),)
CORE_CFLAGS = $(shell $(PKG_CONFIG) --cflags synapse-core)
CORE_LIBS = $(shell $(PKG_CONFIG) --libs synapse-core)
TEST_ENV =
else
CORE_CFLAGS = -I$(CORE_ROOT)$(PREFIX)/include
CORE_LIBS = -L$(CORE_ROOT)$(LIBDIR) -lsynapse-core
TEST_ENV = LD_LIBRARY_PATH=$(CORE_ROOT)$(LIBDIR)
endif

CPPFLAGS ?=
CFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
SOURCES = src/main.c src/parse.c src/render.c
OBJECTS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
TARGET = $(BUILD_DIR)/synapse-doc
REPRO_FLAGS = -ffile-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fdebug-prefix-map=$(abspath $(BUILD_DIR))=build \
	-fmacro-prefix-map=$(abspath $(BUILD_DIR))=build \
	-ffile-prefix-map=$(CURDIR)=. -fdebug-prefix-map=$(CURDIR)=. -fmacro-prefix-map=$(CURDIR)=.

.PHONY: all clean test install
all: $(TARGET)

$(BUILD_DIR):
	install -d -m 0755 "$@"

$(BUILD_DIR)/%.o: src/%.c src/doc_internal.h | $(BUILD_DIR)
	$(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS) $(REPRO_FLAGS) \
		$(CORE_CFLAGS) $(shell $(PKG_CONFIG) --cflags $(PKGS)) -c -o "$@" "$<"

$(TARGET): $(OBJECTS)
	$(CC) -o "$@" $(OBJECTS) $(BASE_LDFLAGS) $(LDFLAGS) $(CORE_LIBS) \
		$(shell $(PKG_CONFIG) --libs $(PKGS)) $(LDLIBS)

test: $(TARGET)
	$(TEST_ENV) ./tests/run.sh "$(abspath $(TARGET))"

install: $(TARGET)
	install -D -m 0755 "$(TARGET)" "$(DESTDIR)$(BINDIR)/synapse-doc"
	install -D -m 0644 data/org.synapse.Doc.desktop "$(DESTDIR)$(DATADIR)/applications/org.synapse.Doc.desktop"
	install -D -m 0644 README.md "$(DESTDIR)$(DATADIR)/doc/synapse-doc/README.md"
	install -D -m 0644 CHANGELOG.md "$(DESTDIR)$(DATADIR)/doc/synapse-doc/CHANGELOG.md"
	install -D -m 0644 docs/architecture.md "$(DESTDIR)$(DATADIR)/doc/synapse-doc/architecture.md"
	install -D -m 0644 docs/json-contracts.md "$(DESTDIR)$(DATADIR)/doc/synapse-doc/json-contracts.md"
	install -D -m 0644 LICENSE "$(DESTDIR)$(DATADIR)/licenses/synapse-doc/LICENSE"

clean:
	rm -rf "$(BUILD_DIR)"
