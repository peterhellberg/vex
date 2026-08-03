// Package xgbquiet silences the two harmless notices jezek/xgb prints when
// DISPLAY is set but ~/.Xauthority can't be read (common under Wayland/Xwayland
// or remote X). The library then falls back to an unauthenticated X11
// connection, which works on any normal desktop session, so those lines are
// pure noise. Anything else xgb logs still goes to stderr unchanged.
//
// The logger must be installed before ebitengine is initialized: ebitengine
// opens an X11 connection from a package init() (internal/ui), which runs
// before main's own init()s. So this package is blank-imported by vex-run as
// its first import, guaranteeing this init() runs first.
package xgbquiet

import (
	"log"
	"os"
	"strings"

	"github.com/jezek/xgb"
)

type authorityFilter struct{}

func (authorityFilter) Write(p []byte) (int, error) {
	if !strings.Contains(string(p), "authority info") {
		os.Stderr.Write(p)
	}

	return len(p), nil
}

func init() {
	xgb.Logger = log.New(authorityFilter{}, "XGB: ", log.Lshortfile)
}
