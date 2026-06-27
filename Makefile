# vex - minimal WASM fantasy console.
#
# The build is defined in build.zig; this Makefile is just a convenience
# wrapper around `zig build`.
#
#   make          build vex + vex-init + vex-web + cart.wasm + zcart.wasm into ./bin
#   make run      run the C example cart
#   make runz     run the Zig example cart
#   make web      serve the browser build (override the cart with CART=...)
#   make test-web run Playwright tests against the browser build (override the cart with CART=...)
#   make install  install the vex + vex-init + vex-web binaries to ~/.local/bin
#   make clean    remove build artifacts

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# Cart served by `make web` and tested by `make test-web`; override e.g.
# `make web CART=bin/zcart.wasm`.
CART ?= bin/cart.wasm

# Path to the bundled test directory (Playwright scripts).
TEST_DIR := cmd/vex-web/test

.PHONY: all run runz web test-web install uninstall clean test-deps docs

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

# Build the bundled cart so the test always exercises the embedded assets
# (not just dev-mode files served by `go run`). `test-deps` is invoked
# lazily so a fresh checkout can `make test-web` and have the npm
# packages and Chromium download happen on first run.
#
# The test script itself removes the `bundle/` directory it creates
# on the way out (success, failure, or exception); the explicit rm
# below is a belt-and-suspenders safety net in case the script is
# killed before its finally{} runs.
test-web: $(TEST_DIR)/node_modules/.package-lock.json
	cd $(TEST_DIR) && node test_gamepad.js $(CURDIR)/$(CART)
	rm -rf $(CURDIR)/bundle

# `npm install` only runs the first time and on dep changes; the
# timestamp file is our cheap "did we install?" marker.
$(TEST_DIR)/node_modules/.package-lock.json: $(TEST_DIR)/package.json
	cd $(TEST_DIR) && npm install --no-audit --no-fund
	cd $(TEST_DIR) && npx --yes playwright install chromium
	@touch $@

# Alias for "I just want the deps to be ready" — useful in CI.
test-deps: $(TEST_DIR)/node_modules/.package-lock.json

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
	rm -rf $(TEST_DIR)/node_modules
