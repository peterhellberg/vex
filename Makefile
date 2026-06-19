# vex - minimal WASM fantasy console.
#
# The build is defined in build.zig; this Makefile is just a convenience
# wrapper around `zig build`.
#
#   make          build ./vex + ./vex-init + cart.wasm + zcart.wasm
#   make run      run the C example cart
#   make runz     run the Zig example cart
#   make install  install the vex + vex-init binaries to ~/.local/bin
#   make clean    remove build artifacts

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all run runz docs install uninstall clean

all:
	zig build

run:
	zig build run

runz:
	zig build runz

docs:
	zig build-lib -fno-emit-bin -femit-docs vex.zig

install: all
	mkdir -p $(BINDIR)
	install -m 0755 zig-out/bin/vex $(BINDIR)/vex
	install -m 0755 zig-out/bin/vex-init $(BINDIR)/vex-init

uninstall:
	rm -f $(BINDIR)/vex $(BINDIR)/vex-init

clean:
	rm -rf zig-out .zig-cache
