# vex - minimal WASM fantasy console.
#
# The build is split across two Zig packages:
#   - the SDK (root build.zig): vex-init, the example carts, the SDK modules
#   - the host (cmd/vex/build.zig): the ./vex C binary, which is the only
#     package that depends on raylib + wasm3.
#
# `make` runs both, installing everything into ./bin so the binaries live
# together rather than scattered across zig-out/bin. Carts go under
# ./bin/carts/.
#
#   make          build vex + vex-init + vex-web + carts into ./bin
#   make run      build, then run the C example cart
#   make runz     build, then run the Zig example cart
#   make web      serve the browser build (override the cart with CART=...)
#   make test-web run Playwright tests against the browser build (override the cart with CART=...)
#   make install  install the vex + vex-init + vex-web + vex-run binaries to ~/.local/bin
#   make clean    remove build artifacts

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# Cart served by `make web` and tested by `make test-web`; override e.g.
# `make web CART=bin/carts/zcart.wasm`.
CART ?= bin/carts/cart.wasm

# Path to the bundled test directory (Playwright scripts).
TEST_DIR := cmd/vex-web/test

.PHONY: all run runz web test-web install uninstall clean test-deps docs

# Build every binary into ./bin. `--prefix .` makes Zig install into ./bin
# (its exe dir under the prefix). The host package is built from cmd/vex/
# with `--prefix ../..` so it lands in the same ./bin.
all:
	zig build --prefix .
	cd cmd/vex && zig build --prefix ../..
	go build -o bin/vex-web ./cmd/vex-web
	go build -o bin/vex-run ./cmd/vex-run

run: all
	cd cmd/vex && zig build run

runz: all
	cd cmd/vex && zig build runz

web:
	zig build --prefix .
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
	zig build --prefix .
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
	rm -rf bin zig-out .zig-cache cmd/vex/zig-out cmd/vex/.zig-cache cmd/vex/zig-pkg
	rm -rf $(TEST_DIR)/node_modules