# vex - minimal WASM fantasy console.
#
# The build is defined in build.zig; this Makefile is just a convenience
# wrapper around `zig build`.
#
#   make          build ./vex + ./vex-init + cart.wasm + zcart.wasm
#   make run      run the C example cart
#   make runz     run the Zig example cart
#   make web      serve the browser build (override the cart with CART=...)
#   make install  install the vex + vex-init + vex-web binaries to ~/.local/bin
#   make clean    remove build artifacts

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# Cart served by `make web`; override e.g. `make web CART=bin/zcart.wasm`.
CART ?= bin/cart.wasm

.PHONY: all run runz web docs install uninstall clean

# `--prefix .` installs into ./bin (Zig's exe dir under the prefix), so the
# Zig binaries land beside the Go vex-web instead of in zig-out/bin.
all:
	zig build --prefix .

run:
	zig build run

runz:
	zig build runz

web: all
	go run ./cmd/vex-web $(CART)

docs:
	zig build-lib -fno-emit-bin -femit-docs vex.zig

install: all
	go build -o bin/vex-web ./cmd/vex-web
	mkdir -p $(BINDIR)
	install -m 0755 bin/vex $(BINDIR)/vex
	install -m 0755 bin/vex-init $(BINDIR)/vex-init
	install -m 0755 bin/vex-web $(BINDIR)/vex-web

uninstall:
	rm -f $(BINDIR)/vex $(BINDIR)/vex-init $(BINDIR)/vex-web

clean:
	rm -rf bin zig-out .zig-cache
