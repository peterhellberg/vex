# vex - minimal WASM fantasy console.
#
# The build is defined in build.zig; this Makefile is just a convenience
# wrapper around `zig build`.
#
#   make          build vex + vex-init + vex-web + cart.wasm + zcart.wasm into ./bin
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

# Build every binary into ./bin. `--prefix .` makes Zig install into ./bin (its
# exe dir under the prefix); vex-web (Go) is built into the same ./bin so all
# binaries live together rather than scattered across zig-out/bin.
all:
	zig build --prefix .
	go build -o bin/vex-web ./cmd/vex-web
	go build -o bin/vex-run ./cmd/vex-run

run:
	zig build run

runz:
	zig build runz

web:
	zig build --prefix . -Dhost=false
	go run ./cmd/vex-web $(CART)

docs:
	zig build-lib -fno-emit-bin -femit-docs vex.zig

install: all
	mkdir -p $(BINDIR)
	install -m 0755 bin/vex $(BINDIR)/vex
	install -m 0755 bin/vex-init $(BINDIR)/vex-init
	install -m 0755 bin/vex-web $(BINDIR)/vex-web
	install -m 0755 bin/vex-run $(BINDIR)/vex-run

uninstall:
	rm -f $(BINDIR)/vex $(BINDIR)/vex-init $(BINDIR)/vex-web

clean:
	rm -rf bin zig-out .zig-cache
