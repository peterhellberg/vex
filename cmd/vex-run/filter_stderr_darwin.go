//go:build darwin

package main

import (
	"bufio"
	"os"
	"strings"

	"golang.org/x/sys/unix"
)

func filterStderr() (restore func()) {
	orig, _ := unix.Dup(2)

	r, w, _ := os.Pipe()
	unix.Dup2(int(w.Fd()), 2)

	os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

	go func() {
		sc := bufio.NewScanner(r)
		for sc.Scan() {
			line := sc.Text()
			if strings.Contains(line, "[CAMetalLayer nextDrawable]") {
				continue
			}

			unix.Write(orig, []byte(line+"\n"))
		}
	}()

	return func() {
		unix.Dup2(orig, 2)

		os.Stderr = os.NewFile(uintptr(2), "/dev/stderr")

		w.Close()
		r.Close()
		unix.Close(orig)
	}
}
