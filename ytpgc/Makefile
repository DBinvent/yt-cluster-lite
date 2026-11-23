# SPDX-License-Identifier: MIT - Copyright (c) 2026 DBinvent
# YT Cluster PostgreSQL Extension Makefile
# Uses PGXS (PostgreSQL Extension Building Infrastructure)
ME = YT Cluster PostgreSQL Extension ytpgc/Makefile
# Extension version: the repo's VERSION file, the one source
YT_CLUSTER_VERSION := $(shell tr -d ' \n' < ../VERSION)
YT_CLUSTER_VERSION_CTL := $(shell echo $(YT_CLUSTER_VERSION) | cut -d. -f1,2 )
YT_CLUSTER_VERSION_SHORT := $(shell echo $(YT_CLUSTER_VERSION_CTL) | sed 's/\./_/g')

EXTENSION = yt_cluster
MODULE_big = yt_cluster
# the script of this version, and the update scripts to it from the earlier ones
# (previous-versions.txt; made from the script by update-sql.py, `make update-sql`)
DATA = yt_cluster--$(YT_CLUSTER_VERSION_CTL).sql $(wildcard yt_cluster--*--$(YT_CLUSTER_VERSION_CTL).sql)
OBJS = src/yt_cluster.o src/grpc_client.o src/protobuf.o src/diff_apply_pg.o \
       src/diff_encode_pg.o src/llm.o src/ycoord.o \
       src/proto/base.pb-c.o src/proto/db.pb-c.o \
       src/proto/diff_doc.pb-c.o src/proto/diff_apply.o \
       src/proto/bson_doc.o

# ── diff-doc-rs: its own public repo, a sibling of this one ───────────────────
# diff_doc.proto and the C sources are copied from it (into ../yt-core-proto/proto
# and src/proto) when newer; without the sibling, the committed copies are used.
DIFF_DOC_RS_DIR := $(abspath ../../diff-doc-rs)
DIFFRS_GIT_URL  = https://github.com/vkrinitsyn/diff-doc-rs.git
DIFFRS_HINT     = git -C ../.. clone $(DIFFRS_GIT_URL)
# the copies and the generated sources keep short dashes, as the rest of the repo
DEDASH          = sed -i 's/\xe2\x80\x94/-/g'

# Wildcard: non-empty only when the sibling repo is actually present.
# This lets make track freshness when it is there, and skip the rule
# (using the already-committed copy) when it is not.
DIFFRS_PROTO_SRC := $(wildcard $(DIFF_DOC_RS_DIR)/proto/diff_doc.proto)
DIFFRS_C_SRC     := $(wildcard $(DIFF_DOC_RS_DIR)/c/diff_apply.c)
DIFFRS_H_SRC     := $(wildcard $(DIFF_DOC_RS_DIR)/c/diff_apply.h)
DIFFRS_BSON_C    := $(wildcard $(DIFF_DOC_RS_DIR)/c/bson_doc.c)
DIFFRS_BSON_H    := $(wildcard $(DIFF_DOC_RS_DIR)/c/bson_doc.h)

# Protobuf-C code generation
# base.proto and db.proto: the yt-core-proto crate's, the one copy; diff_doc.proto there
# is diff-doc-rs's, copied
PROTO_DIR = ../yt-core-proto/proto
PROTO_OUT = src/proto
PROTOS = base.proto db.proto diff_doc.proto
PROTO_SRCS = $(PROTOS:%.proto=$(PROTO_OUT)/%.pb-c.c)
PROTO_HDRS = $(PROTOS:%.proto=$(PROTO_OUT)/%.pb-c.h)

# Get PostgreSQL major version
# the major alone: its leading digits. A pre-release says "19rc2", and Debian's adds
# "(Ubuntu 19~rc2-1...)" - cut at the first dot, that was a file name with a space in it
PG_VERSION := $(shell pg_config --version | sed 's/^PostgreSQL \([0-9][0-9]*\).*/\1/')

# Libc tag used in versioned artifact names.
# Override on the make command line for non-glibc builds: make LIBC_SUFFIX=musl
GLIBC_VERSION := $(shell ldd --version 2>&1 | head -n 1 | awk '{print $$NF}' | sed 's/\./_/g')
LIBC_SUFFIX   ?= glibc$(GLIBC_VERSION)

# Versioned library names
VERSIONED_SO = yt_cluster$(YT_CLUSTER_VERSION_SHORT)-pg$(PG_VERSION)-$(LIBC_SUFFIX).so
VERSIONED_A  = libyt_cluster$(YT_CLUSTER_VERSION_SHORT)-pg$(PG_VERSION)-$(LIBC_SUFFIX).a

# Compiler flags
PG_CPPFLAGS = -I$(shell pg_config --includedir) -I./src -I./$(PROTO_OUT) \
              -DYT_CLUSTER_VERSION='"$(YT_CLUSTER_VERSION_CTL)"' \
              -DYT_CLUSTER_DESCRIPTION='"YT+RPPD Cluster PostgreSQL Extension (C, ytcc $(YT_CLUSTER_VERSION_CTL))"'
PG_CFLAGS = -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration

# Required libraries: libcurl for HTTP/2
SHLIB_LINK = -lcurl -lprotobuf-c

# Use PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Ensure proto headers and diff_apply.h exist before hand-written C is compiled
src/yt_cluster.o src/grpc_client.o src/protobuf.o src/diff_apply_pg.o src/diff_encode_pg.o: $(PROTO_HDRS)
src/proto/diff_apply.o src/diff_apply_pg.o: $(PROTO_OUT)/diff_apply.h
src/proto/bson_doc.o src/diff_apply_pg.o src/proto/bson_doc.bc src/diff_apply_pg.bc: $(PROTO_OUT)/bson_doc.h

# ── Dependency sync: copy files from sibling repos when present ───────────────
# Each rule has the sibling-repo file as a (wildcard) prerequisite so make
# auto-updates the copy whenever the source is newer.  If the destination is
# missing AND the source repo is absent, a helpful error is shown.
$(PROTO_DIR)/diff_doc.proto: $(DIFFRS_PROTO_SRC)
	@test -f "$(DIFF_DOC_RS_DIR)/proto/diff_doc.proto" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	cp -f "$(DIFF_DOC_RS_DIR)/proto/diff_doc.proto" $@
	sed -i 's|%LPH%| Copied from diff-doc-rs/proto/diff_doc.proto, by $(ME) |g' $@
	$(DEDASH) $@

$(PROTO_OUT)/diff_apply.c: $(DIFFRS_C_SRC) | $(PROTO_OUT)
	@test -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.c" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	cp -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.c" $@
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/diff_apply.c, by $(ME) |g' $@
	$(DEDASH) $@
	sed -i 's|#include "diff_apply.h"|#include "diff_apply.h"\n\n/* Suppress C89 declaration-after-statement warnings in this C99 file. */\n#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"|' $@
	sed -i 's|static DocError make_error(const char \*fmt, \.\.\.) {|static DocError make_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));\n\nstatic DocError make_error(const char *fmt, ...) {|' $@

$(PROTO_OUT)/diff_apply.h: $(DIFFRS_H_SRC) | $(PROTO_OUT)
	@test -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.h" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	cp -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.h" $@
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/diff_apply.h, by $(ME) |g' $@
	$(DEDASH) $@

# BSON of DocumentDB / MongoDB as a GenericValue, and back (yt_diff_apply_bson)
$(PROTO_OUT)/bson_doc.c: $(DIFFRS_BSON_C) | $(PROTO_OUT)
	@test -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.c" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	cp -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.c" $@
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/bson_doc.c, by $(ME) |g' $@
	$(DEDASH) $@
	sed -i 's|#include "bson_doc.h"|#include "bson_doc.h"\n\n/* Suppress C89 declaration-after-statement warnings in this C99 file. */\n#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"|' $@

$(PROTO_OUT)/bson_doc.h: $(DIFFRS_BSON_H) | $(PROTO_OUT)
	@test -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.h" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	cp -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.h" $@
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/bson_doc.h, by $(ME) |g' $@
	$(DEDASH) $@

# Post-build: versioned .so and .a, remove unversioned yt_cluster.so
all: $(PROTO_SRCS) $(VERSIONED_SO) $(VERSIONED_A) $(EXTENSION).control

$(VERSIONED_SO): yt_cluster.so
	cp $< $@
	rm -f $<
	@echo "Created versioned library: $(VERSIONED_SO)"
	ls -lah $(VERSIONED_SO)

$(VERSIONED_A): $(OBJS)
	ar rcs $@ $(OBJS)
	@echo "Created versioned archive: $(VERSIONED_A)"
	ls -lah $(VERSIONED_A)

# Update control file version (major.minor only)
$(EXTENSION).control: $(EXTENSION).control.in ../VERSION
	sed -e 's/@YT_CLUSTER_VERSION@/$(YT_CLUSTER_VERSION_CTL)/g' $< > $@

# The update scripts, yt_cluster--<earlier>--<version>.sql: written from this version's
# script (update-sql.py), committed, and run by ALTER EXTENSION yt_cluster UPDATE in a
# database that has an earlier version. After a change of the script, and after a new
# major.minor in ../VERSION (its earlier one goes into previous-versions.txt first):
update-sql:
	python3 update-sql.py

check-update-sql:
	python3 update-sql.py --check

# Add permission check as prerequisite of installdirs (runs before any file copying)
installdirs: check-install-perms

.PHONY: clean-all info proto check-install-perms check-deps sync-deps update-sql check-update-sql

check-deps:
	@test -d "$(DIFF_DOC_RS_DIR)" || { \
	  echo ""; \
	  echo "  Missing dependency: diff-doc-rs"; \
	  echo "  Clone with:  $(DIFFRS_HINT)"; \
	  echo ""; exit 1; }
	@echo "Dependencies OK: diff-doc-rs found."

sync-deps: check-deps | $(PROTO_OUT)
	cp -f "$(DIFF_DOC_RS_DIR)/proto/diff_doc.proto" "$(PROTO_DIR)/diff_doc.proto"
	sed -i 's|%LPH%| Copied from diff-doc-rs/proto/diff_doc.proto, by $(ME) |g' "$(PROTO_DIR)/diff_doc.proto"
	$(DEDASH) "$(PROTO_DIR)/diff_doc.proto"
	cp -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.c" "$(PROTO_OUT)/diff_apply.c"
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/diff_apply.c, by $(ME) |g' "$(PROTO_OUT)/diff_apply.c"
	sed -i 's|#include "diff_apply.h"|#include "diff_apply.h"\n\n/* Suppress C89 declaration-after-statement warnings in this C99 file. */\n#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"|' "$(PROTO_OUT)/diff_apply.c"
	sed -i 's|static DocError make_error(const char \*fmt, \.\.\.) {|static DocError make_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));\n\nstatic DocError make_error(const char *fmt, ...) {|' "$(PROTO_OUT)/diff_apply.c"
	cp -f "$(DIFF_DOC_RS_DIR)/c/diff_apply.h" "$(PROTO_OUT)/diff_apply.h"
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/diff_apply.h, by $(ME) |g' "$(PROTO_OUT)/diff_apply.h"
	cp -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.c" "$(PROTO_OUT)/bson_doc.c"
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/bson_doc.c, by $(ME) |g' "$(PROTO_OUT)/bson_doc.c"
	sed -i 's|#include "bson_doc.h"|#include "bson_doc.h"\n\n/* Suppress C89 declaration-after-statement warnings in this C99 file. */\n#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"|' "$(PROTO_OUT)/bson_doc.c"
	cp -f "$(DIFF_DOC_RS_DIR)/c/bson_doc.h" "$(PROTO_OUT)/bson_doc.h"
	sed -i 's|%LPH%| Copied from diff-doc-rs/c/bson_doc.h, by $(ME) |g' "$(PROTO_OUT)/bson_doc.h"
	$(DEDASH) $(PROTO_OUT)/diff_apply.c $(PROTO_OUT)/diff_apply.h $(PROTO_OUT)/bson_doc.c $(PROTO_OUT)/bson_doc.h
	@echo "Synced all dependency files."

check-install-perms:
	@test -w $(shell $(PG_CONFIG) --pkglibdir) || { \
	  echo ""; \
	  echo "  Permission denied: $(shell $(PG_CONFIG) --pkglibdir)"; \
	  echo "  Run: sudo make install"; \
	  echo ""; \
	  exit 1; \
	}

proto: $(PROTO_SRCS)

# Generate all proto files in one invocation via a stamp file
PROTO_STAMP = $(PROTO_OUT)/.stamp

$(PROTO_SRCS) $(PROTO_HDRS): $(PROTO_STAMP)

$(PROTO_STAMP): $(addprefix $(PROTO_DIR)/,$(PROTOS)) | $(PROTO_OUT)
	protoc --proto_path=$(PROTO_DIR) --c_out=$(PROTO_OUT) $(PROTOS)
	# Fix: rename user proto fields named 'base' that clash with ProtobufCMessage base member
	sed -i 's/Base__BaseApi \*base;/Base__BaseApi *base_api;/g' $(PROTO_OUT)/base.pb-c.h $(PROTO_OUT)/base.pb-c.c
	sed -i 's/offsetof(Base__CommonReply, base)/offsetof(Base__CommonReply, base_api)/g' $(PROTO_OUT)/base.pb-c.c
	sed -i 's/offsetof(Base__ClusterComplete, base)/offsetof(Base__ClusterComplete, base_api)/g' $(PROTO_OUT)/base.pb-c.c
	$(DEDASH) $(PROTO_SRCS) $(PROTO_HDRS)
	touch $@

$(PROTO_OUT):
	mkdir -p $(PROTO_OUT)

clean-all: clean
	rm -f src/*.o src/*.bc yt_cluster*.so libyt_cluster*.a yt_cluster.control
	rm -f $(PROTO_OUT)/*.pb-c.c $(PROTO_OUT)/*.pb-c.h \
	      $(PROTO_OUT)/*.o $(PROTO_OUT)/*.bc \
	      $(PROTO_OUT)/.stamp

info:
	@echo "Extension: $(EXTENSION)"
	@echo "Version: $(YT_CLUSTER_VERSION)"
	@echo "PostgreSQL: $(PG_VERSION)"
	@echo "libc:  $(LIBC_SUFFIX)"
	@echo "Binary: $(VERSIONED_SO)"
	@test -f $(VERSIONED_SO) && ls -lah $(VERSIONED_SO) || echo "NOT BUILT: $(VERSIONED_SO)"
