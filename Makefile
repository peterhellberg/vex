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

# Cart served by `make web`; override e.g. `make web CART=zig-out/bin/zcart.wasm`.
CART ?= zig-out/bin/cart.wasm

.PHONY: all run runz web docs install uninstall clean

all:
	zig build

run:
	zig build run

runz:
	zig build runz

web: all
	go run tools/vex-web.go $(CART)

docs:
	zig build-lib -fno-emit-bin -femit-docs vex.zig

install: all
	go build -o zig-out/bin/vex-web ./tools/vex-web.go
	mkdir -p $(BINDIR)
	install -m 0755 zig-out/bin/vex $(BINDIR)/vex
	install -m 0755 zig-out/bin/vex-init $(BINDIR)/vex-init
	install -m 0755 zig-out/bin/vex-web $(BINDIR)/vex-web

uninstall:
	rm -f $(BINDIR)/vex $(BINDIR)/vex-init $(BINDIR)/vex-web

clean:
	rm -rf zig-out .zig-cache
