# Convenience wrapper around idf.py and doxygen.
#
#   make              build firmware
#   make flash        flash (port auto-detected; override with PORT=/dev/ttyACMx)
#   make monitor      serial monitor
#   make run          flash + monitor
#   make menuconfig   edit sdkconfig
#   make docs         generate Doxygen HTML into docs/html
#
# ESP-IDF is activated per command via IDF_ACTIVATE, so no prior
# `source export.sh` is needed. Override if IDF lives elsewhere:
#   make IDF_ACTIVATE=/path/to/activate_idf_vX.sh

SHELL        := /bin/bash
IDF_ACTIVATE ?= $(HOME)/.espressif/tools/activate_idf_v6.1.sh
PORT         ?=

IDF := source "$(IDF_ACTIVATE)" >/dev/null 2>&1 && idf.py
PORT_ARG := $(if $(PORT),-p $(PORT))

DOXYGEN  ?= doxygen
DOCS_DIR := docs

export GIZMO_VERSION  := $(shell git describe --tags --always --dirty 2>/dev/null)
export GIZMO_HAVE_DOT := $(if $(shell command -v dot 2>/dev/null),YES,NO)

.PHONY: all build flash monitor run menuconfig size clean fullclean docs docs-open docs-clean help

all: build

build:
	$(IDF) build

flash:
	$(IDF) $(PORT_ARG) flash

monitor:
	$(IDF) $(PORT_ARG) monitor

run:
	$(IDF) $(PORT_ARG) flash monitor

menuconfig:
	$(IDF) menuconfig

size:
	$(IDF) size

clean:
	$(IDF) clean

fullclean:
	$(IDF) fullclean

docs:
	@command -v $(DOXYGEN) >/dev/null || { echo "doxygen not found (apt install doxygen graphviz)"; exit 1; }
	@mkdir -p $(DOCS_DIR)
	$(DOXYGEN) Doxyfile
	@if [ -s $(DOCS_DIR)/warnings.log ]; then echo "Doxygen warnings:"; cat $(DOCS_DIR)/warnings.log; fi
	@echo "Docs: $(DOCS_DIR)/html/index.html"

docs-open: docs
	xdg-open $(DOCS_DIR)/html/index.html >/dev/null 2>&1 &

docs-clean:
	rm -rf $(DOCS_DIR)

help:
	@sed -n '2,10p' Makefile | sed 's/^# \{0,1\}//'
