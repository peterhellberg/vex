//go:build !darwin

package main

func filterStderr() func() {
	return func() {}
}
