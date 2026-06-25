package main

import (
	"fmt"
	"image"
	"image/png"
	"os"
	"strings"
)

func main() {
	if err := run(os.Args); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

func run(args []string) error {
	if len(args) < 2 {
		return fmt.Errorf("Usage: vex-png-to-spr image.png # Creates image.spr")
	}

	src := args[1]

	base, found := strings.CutSuffix(src, ".png")
	if !found {
		return fmt.Errorf("input does not seem to be a .png")
	}

	f, err := os.Open(src)
	if err != nil {
		return fmt.Errorf("failed to open input: %w", err)
	}
	defer f.Close()

	img, err := png.Decode(f)
	if err != nil {
		return fmt.Errorf("failed to decode png: %w", err)
	}

	pal, ok := img.(*image.Paletted)
	if !ok {
		return fmt.Errorf("error: PNG is not indexed (paletted).")
	}

	var (
		data = pal.Pix
		dx   = pal.Bounds().Dx()
		dy   = pal.Bounds().Dy()
		dst  = fmt.Sprintf("%s-%dx%d.spr", base, dx, dy)
	)

	out, err := os.Create(dst)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer out.Close()

	_, err = out.Write(data)
	if err != nil {
		return fmt.Errorf("failed to write output: %w", err)
	}

	fmt.Printf("vex-png-to-spr: wrote %d bytes to %s\n", len(data), dst)

	return nil
}
