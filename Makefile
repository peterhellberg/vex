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
#   make test-hosts run the Go engine/conformance tests plus the C mixer
#                    harness (main.c's audio section compiled against stubs)
#   make install  install the vex + vex-init + vex-web + vex-run binaries to ~/.local/bin
#   make release          cross-compile release archives for Linux, Windows and
#                         macOS into ./release
#   make release-linux    build & archive just the Linux build
#   make release-windows  build & archive just the Windows build
#   make release-macos    build & archive just the macOS build
#   make clean            remove build artifacts

PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

# Cart served by `make web` and tested by `make test-web`; override e.g.
# `make web CART=bin/carts/zcart.wasm`.
CART ?= bin/carts/cart.wasm

# Path to the bundled test directory (Playwright scripts).
TEST_DIR := cmd/vex-web/test

# Recipes are silent and say what they are doing through STEP, so the output is
# the work rather than the command lines that did it. The tools themselves still
# write to the terminal untouched.
STEP := @printf "\033[32m==>\033[0m %s\n"

# Do not print entering directories
MAKEFLAGS += --no-print-directory

.PHONY: all run runz web test test-web test-hosts
.PHONY: install uninstall clean distclean test-deps docs help
.PHONY: release release-linux release-windows release-macos

# Version stamped into every archive name. Single source of truth: the SDK
# package's build.zig.zon (which is also what `zig build` reports).
VERSION := $(shell awk -F'"' '/\.version/ {print $$2; exit}' build.zig.zon)

# Per-platform staging root. Each recipe builds into its own subdir so the
# three cross-compile outputs can't clobber the native ./bin or each other's
# zig caches (the host package's raylib build is per-target).
RELEASE_DIR := release
STAGING_DIR := $(RELEASE_DIR)/staging

##@ Build

# Build every binary into ./bin. `--prefix .` makes Zig install into ./bin
# (its exe dir under the prefix). The host package is built from cmd/vex/
# with `--prefix ../..` so it lands in the same ./bin.
all: ## Build vex + vex-init + vex-web + carts into ./bin
	$(STEP) "Building vex"
	@zig build --prefix . --release=fast
	@cd cmd/vex && zig build --prefix ../.. --release=fast
	go build -o bin/vex-web ./cmd/vex-web
	go build -o bin/vex-run ./cmd/vex-run

run: all ## Build and run the C example cart
	$(STEP) "Running C cart"
	@cd cmd/vex && zig build run

runz: all ## Build and run the Zig example cart
	$(STEP) "Running Zig cart"
	@cd cmd/vex && zig build runz

web: ## Serve the browser build (CART=... to override)
web:
	$(STEP) "Serving browser build"
	@zig build --prefix .
	@go run ./cmd/vex-web $(CART)

# Build the bundled cart so the test always exercises the embedded assets
# (not just dev-mode files served by `go run`). `test-deps` is invoked
# lazily so a fresh checkout can `make test-web` and have the npm
# packages and Chromium download happen on first run.
#
# The test script itself removes the `bundle/` directory it creates
# on the way out (success, failure, or exception); the explicit rm
# below is a belt-and-suspenders safety net in case the script is
# killed before its finally{} runs.
# --- test-hosts ------------------------------------------------------------
#
# Cross-host tone() conformance:
#   - cmd/vex-run: Go engine unit tests (envelope corners, duty flip point,
#     slide target, noise LFSR, note mode) and golden cart renders
#   - root: shared-table conformance (duty cycles, pan gains, soft-clip knee
#     pinned identically across all three hosts' sources)
#   - cmd/vex: the real audio section extracted from main.c and driven
#     through the same trigger path carts use, compiled with zig cc (the
#     only C compiler this repo uses)
##@ Test

test-hosts: ## Run Go and C conformance tests
	$(STEP) "Running conformance tests"
	@cd cmd/vex-run && go test ./...
	@go test ./...
	@awk '/^\/\/ ---- audio \(tone\)/{on=1} /^static M3Result link_host/{on=0} on' \
		cmd/vex/main.c > cmd/vex/test/audio_section.inc
	@zig cc -std=c2x -Wno-gcc-install-dir-libstdcxx -I cmd/vex/test -o cmd/vex/test/tone_test.bin \
		cmd/vex/test/tone_test.c -lm -lpthread
	@cmd/vex/test/tone_test.bin
	@rm -f cmd/vex/test/audio_section.inc cmd/vex/test/tone_test.bin

test-web: $(TEST_DIR)/node_modules/.package-lock.json ## Run Playwright browser tests
	$(STEP) "Running browser tests"
	@zig build --prefix .
	@cd $(TEST_DIR) && node test_gamepad.js $(CURDIR)/$(CART)
	@rm -rf $(CURDIR)/bundle

test: test-hosts test-web ## Run all tests

# `npm install` only runs the first time and on dep changes; the
# timestamp file is our cheap "did we install?" marker.
$(TEST_DIR)/node_modules/.package-lock.json: $(TEST_DIR)/package.json
	cd $(TEST_DIR) && npm install --no-audit --no-fund
	cd $(TEST_DIR) && npx --yes playwright install chromium
	@touch $@

# Alias for "I just want the deps to be ready" — useful in CI.
test-deps: $(TEST_DIR)/node_modules/.package-lock.json ## Install test dependencies

##@ Docs
# Regenerates everything under docs/ (index.html, main.js, main.wasm,
# sources.tar) from vex.zig. The artifacts are committed for GitHub Pages --
# don't hand-edit them, rerun this target instead.
docs: ## Regenerate docs from vex.zig
	$(STEP) "Generating docs"
	@rm -rf docs .docs-tmp
	@mkdir -p .docs-tmp
	@cp vex.zig .docs-tmp/
	@cd .docs-tmp && zig build-lib -fno-emit-bin -femit-docs=../docs vex.zig
	@rm -rf .docs-tmp

##@ Install
install: all ## Install binaries to ~/.local/bin
	$(STEP) "Installing to $(BINDIR)"
	@mkdir -p $(BINDIR)
	install -m 0755 bin/vex $(BINDIR)/vex
	install -m 0755 bin/vex-init $(BINDIR)/vex-init
	install -m 0755 bin/vex-web $(BINDIR)/vex-web
	install -m 0755 bin/vex-run $(BINDIR)/vex-run

uninstall: ## Remove installed binaries
	$(STEP) "Uninstalling"
	@rm -f $(BINDIR)/vex $(BINDIR)/vex-init $(BINDIR)/vex-web $(BINDIR)/vex-run

##@ Maintenance
# Remove build artifacts. Fetched Zig packages (zig-pkg/) and the Playwright
# node_modules are deliberately kept so the next build starts warm.
clean: ## Remove build artifacts
	$(STEP) "Cleaning"
	@rm -rf bin zig-out .zig-cache cmd/vex/zig-out cmd/vex/.zig-cache
	@rm -rf $(RELEASE_DIR)

# Additionally drop everything fetched from the network: the vendored Zig
# packages (raylib, wasm3, xcode_frameworks under cmd/vex/zig-pkg) and the
# Playwright node_modules/Chromium install. Back to a fresh-checkout state;
# the next build re-fetches and recompiles dependencies from scratch.
distclean: clean ## Remove all build and fetched artifacts
	$(STEP) "Cleaning all"
	@rm -rf zig-pkg cmd/vex/zig-pkg
	@rm -rf $(TEST_DIR)/node_modules

# Targets are documented by a ## comment on the target line, and
# grouped by the ##@ markers above them. Anything without a ## is
# plumbing and stays out of the list.
help: ## Show this help text
	@printf "Usage: make \033[32m<target>\033[0m, or plain \033[32mmake\033[0m to build.\n"
	@awk 'BEGIN {FS = ":.*?## "} \
    /^##@ / {printf "\n\033[1m%s\033[0m\n", substr($$0, 5); next} \
    /^[a-zA-Z_\/-]+:.*?## / {printf "  \033[32m%-29s\033[0m %s\n", $$1, $$2}' \
    $(MAKEFILE_LIST)


# --- release ---------------------------------------------------------------
#
# Three target recipes (linux, windows, macos) plus an aggregate `release` that
# builds all three. Each recipe:
#
#   1. builds the SDK package (vex-init + the example carts) for the target
#   2. builds the host package (vex) for the target from cmd/vex/
#   3. builds vex-web with the matching GOOS/GOARCH
#   4. stages the three binaries under release/staging/<os>-<arch>/vex-<ver>/
#   5. packs an archive at release/vex-<ver>-<os>-<arch>.{tar.gz,zip}
#
# vex-run is intentionally NOT included: it depends on ebitengine (cgo, not
# cross-compilable) and on syscall.Dup2 (Linux/macOS only), so shipping it
# would either bloat the Windows/macOS archives with a non-functional binary
# or force a "current host only" carve-out that complicates the matrix.
#
# Per-target -Dtarget values:
#   linux:    -Dtarget=native         (system X11/GL, distro-agnostic; we
#                                      don't pin a glibc version so the
#                                      binary runs on any modern distro)
#   windows:  -Dtarget=x86_64-windows-gnu   (full cross-compile, MinGW
#                                           import libs come from Zig)
#   macos:    -Dtarget=aarch64-macos   (cross-compile via the xcode_frameworks
#                                     lazy dep, which stubs AppKit/IOKit on
#                                     non-macOS hosts so raylib links)
#
# The SDK build also installs the example + test carts into
# <staging>/vex-<ver>/bin/carts/; they're small (a few hundred KB total) and
# let the user try `vex cart.wasm` immediately after unpacking. Strip them
# here if a future policy wants a binary-only archive.

# Build vex-web into <staging>/<target>/vex-$(VERSION)/bin/ alongside the
# Zig-installed vex + vex-init binaries.
define vex-web-cross
	GOOS=$(1) GOARCH=$(2) CGO_ENABLED=0 \
		go build -trimpath -ldflags='-s -w' \
			-o $(STAGING_DIR)/$(3)/bin/vex-web$(4) ./cmd/vex-web
endef

# Binaries aren't stripped: the Zig builds have no -Dstrip equivalent in
# 0.17 and the right strip flags depend on host OS vs target format, which
# isn't worth the complexity here. The Go binary is already stripped at
# build time via -s -w.

##@ Release

# Linux: build native. The SDK package has no raylib dep, so the
# linux_display_backend option is host-only; we forward it to the host
# build.zig (cmd/vex/) where it controls which GL/display backend raylib
# pulls in. The default is X11; a Wayland-only user can override via the
# environment without editing the Makefile.
release-linux: export LINUX_DISPLAY_BACKEND ?= X11
release-linux: ## Build Linux release archive
	$(STEP) "Building Linux release"
	@mkdir -p $(STAGING_DIR)/linux-amd64/vex-$(VERSION)
	@zig build --prefix $(CURDIR)/$(STAGING_DIR)/linux-amd64/vex-$(VERSION) \
		-Dtarget=native --release=fast
	@cd cmd/vex && zig build --prefix $(CURDIR)/$(STAGING_DIR)/linux-amd64/vex-$(VERSION) \
		-Dtarget=native --release=fast -Dlinux_display_backend=$(LINUX_DISPLAY_BACKEND)
	@$(call vex-web-cross,linux,amd64,linux-amd64/vex-$(VERSION),)
	@cp README.md $(STAGING_DIR)/linux-amd64/vex-$(VERSION)/
	@cp LICENSE $(STAGING_DIR)/linux-amd64/vex-$(VERSION)/
	@tar -czf $(CURDIR)/$(RELEASE_DIR)/vex-$(VERSION)-linux-amd64.tar.gz \
		-C $(STAGING_DIR)/linux-amd64 vex-$(VERSION)
	@echo "==> $(RELEASE_DIR)/vex-$(VERSION)-linux-amd64.tar.gz"

# Windows: cross-compile. raylib + wasm3 + the wasm3 -Dd_m3Use32BitSlots=0
# flag all flow through the host build.zig unchanged.
release-windows: ## Build Windows release archive
	$(STEP) "Building Windows release"
	@mkdir -p $(STAGING_DIR)/windows-amd64/vex-$(VERSION)
	@zig build --prefix $(CURDIR)/$(STAGING_DIR)/windows-amd64/vex-$(VERSION) \
		-Dtarget=x86_64-windows-gnu --release=fast
	@cd cmd/vex && zig build --prefix $(CURDIR)/$(STAGING_DIR)/windows-amd64/vex-$(VERSION) \
		-Dtarget=x86_64-windows-gnu --release=fast
	@$(call vex-web-cross,windows,amd64,windows-amd64/vex-$(VERSION),.exe)
	@cp README.md $(STAGING_DIR)/windows-amd64/vex-$(VERSION)/
	@cp LICENSE $(STAGING_DIR)/windows-amd64/vex-$(VERSION)/
	@find $(STAGING_DIR)/windows-amd64 -name "*.pdb" -delete
	# Windows binaries aren't stripped: `strip` is platform-aware and the
	# Linux toolchain can't touch a PE file.
	@(cd $(STAGING_DIR)/windows-amd64 && \
		zip -qr $(CURDIR)/$(RELEASE_DIR)/vex-$(VERSION)-windows-amd64.zip vex-$(VERSION))
	@echo "==> $(RELEASE_DIR)/vex-$(VERSION)-windows-amd64.zip"

# macOS: cross-compile via the xcode_frameworks stubs. The host build.zig
# detects `.macos` and adds the stub include + framework + lib paths to
# raylib; nothing extra is needed at the Makefile level.
release-macos: ## Build macOS release archive
	$(STEP) "Building macOS release"
	@mkdir -p $(STAGING_DIR)/macos-aarch64/vex-$(VERSION)
	@zig build --prefix $(CURDIR)/$(STAGING_DIR)/macos-aarch64/vex-$(VERSION) \
		-Dtarget=aarch64-macos --release=fast
	@cd cmd/vex && zig build --prefix $(CURDIR)/$(STAGING_DIR)/macos-aarch64/vex-$(VERSION) \
		-Dtarget=aarch64-macos --release=fast
	@$(call vex-web-cross,darwin,arm64,macos-aarch64/vex-$(VERSION),)
	@cp README.md $(STAGING_DIR)/macos-aarch64/vex-$(VERSION)/
	@cp LICENSE $(STAGING_DIR)/macos-aarch64/vex-$(VERSION)/
	# macOS binaries aren't stripped on a Linux host: the Mach-O strip tools
	# (otool, dsymutil, llvm-strip with the right target) aren't reliably
	# available, and llvm-strip on the wrong target would corrupt the
	# binary. Users on macOS can strip manually if they care.
	@tar -czf $(CURDIR)/$(RELEASE_DIR)/vex-$(VERSION)-macos-aarch64.tar.gz \
		-C $(STAGING_DIR)/macos-aarch64 vex-$(VERSION)
	@echo "==> $(RELEASE_DIR)/vex-$(VERSION)-macos-aarch64.tar.gz"

release: release-linux release-windows release-macos ## Build all release archives
	$(STEP) "Release archives"
	@echo ""
	@echo "Release archives:"
	@ls -lh $(RELEASE_DIR)/*.tar.gz $(RELEASE_DIR)/*.zip 2>/dev/null
