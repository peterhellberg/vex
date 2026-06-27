// Command vex-web is a small self-contained web server for the browser build
// of the vex fantasy console.
//
// Usage:
//
//	go run github.com/peterhellberg/vex/cmd/vex-web@latest [-addr host:port] <cart.wasm>
//	go run ./cmd/vex-web -bundle <cart.wasm>
//
// index.html and vex.js are embedded into the binary (from cmd/vex-web/assets),
// so a built vex-web needs no files alongside it. The cart given as the first
// argument is served on /cart.wasm, read from disk on every request — so
// rebuilding the cart and refreshing the page picks up the new bytes without
// restarting the server.
//
// /cart.wasm is the default cart the page loads on start; drag-and-dropping a
// different .wasm onto the page still works and is handled entirely in the
// browser.
//
// With -bundle, vex-web writes a static bundle for the cart instead of serving
// it: index.html, vex.js and the cart wasm are written to bundle/<name>/ (where
// <name> is the cart's base name without extension) and the directory is also
// archived to bundle/<name>.zip. The bundle runs the cart without any server,
// so it can be hosted as plain static files. The paths it wrote are reported on
// stdout; everything else (warnings, server logs) goes to stderr.
package main

import (
	"archive/zip"
	_ "embed"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"
)

//go:embed assets/index.html
var indexHTML []byte

//go:embed assets/vex.js
var vexJS []byte

// Input holds the parsed command-line flags.
type Input struct {
	addr   string
	noOpen bool
	poll   time.Duration
	bundle bool
}

func main() {
	if err := run(os.Args, os.Stdout, os.Stderr); err != nil && !errors.Is(err, flag.ErrHelp) {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

// run parses args, wires up the server and serves until it errors. It takes its
// arguments and output explicitly so it can be exercised without touching
// process globals. Diagnostics go to stderr; the bundle command writes its
// result to stdout.
func run(args []string, stdout, stderr io.Writer) error {
	var in Input

	fs := flag.NewFlagSet(args[0], flag.ContinueOnError)
	fs.SetOutput(stderr)

	fs.StringVar(&in.addr, "addr", ":8383", "address to listen on")
	fs.BoolVar(&in.noOpen, "no-open", false, "don't open the browser on start")
	fs.DurationVar(&in.poll, "poll", 500*time.Millisecond, "how often to stat the cart for live-reload")
	fs.BoolVar(&in.bundle, "bundle", false, "write a static bundle for the cart to bundle/<name>/ (and .zip) instead of serving")

	fs.Usage = func() {
		fmt.Fprintf(stderr,
			"usage: %s [-addr host:port] [-no-open] [-bundle] <cart.wasm>\n",
			filepath.Base(args[0]))
		fs.PrintDefaults()
	}

	if err := fs.Parse(args[1:]); err != nil {
		return err
	}

	cart := fs.Arg(0)

	if cart == "" {
		fs.Usage()
		return errors.New("no cart specified")
	}

	if in.bundle {
		return writeBundle(cart, stdout)
	}

	// Warn early if the cart is missing, but keep serving: it may be (re)built
	// after the server starts.
	if _, err := os.Stat(cart); err != nil {
		fmt.Fprintf(stderr, "warning: %v\n", err)
	}

	mux := http.NewServeMux()

	mux.HandleFunc("/vex.js", serveBytes(vexJS, "text/javascript; charset=utf-8"))
	mux.HandleFunc("/cart.wasm", serveCart(cart, stderr))
	mux.HandleFunc("/reload", serveReload(cart, in.poll))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}

		serveBytes(indexHTML, "text/html; charset=utf-8")(w, r)
	})

	// Bind the listener before opening the browser so the page never races
	// ahead of the server and hits a connection refused.
	ln, err := listen(in.addr, 5, stderr)
	if err != nil {
		return err
	}

	url := serverURL(ln.Addr().String())

	fmt.Fprintf(stderr, "serving cart %s on %s\n", displayPath(cart), url)

	if !in.noOpen {
		if err := openBrowser(url); err != nil {
			fmt.Fprintf(stderr, "could not open browser: %v (visit %s)\n", err, url)
		}
	}

	return http.Serve(ln, mux)
}

// listen binds a TCP listener to addr, retrying on the next port number (up to
// tries attempts) whenever the chosen port is already in use.
func listen(addr string, tries int, stderr io.Writer) (net.Listener, error) {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, err
	}

	n, err := strconv.Atoi(port)
	if err != nil {
		return nil, err
	}

	for i := range tries {
		a := net.JoinHostPort(host, strconv.Itoa(n+i))

		ln, err := net.Listen("tcp", a)
		if err == nil {
			return ln, nil
		}

		if !errors.Is(err, syscall.EADDRINUSE) {
			return nil, err
		}

		if i < tries-1 {
			fmt.Fprintf(stderr, "port %d in use, trying %d\n", n+i, n+i+1)
		}
	}

	return nil, fmt.Errorf("no free port after %d attempts starting at %s", tries, addr)
}

// displayPath shortens an absolute cart path to one relative to the current
// directory for logging — e.g. zig build (which passes an absolute installed
// path) shows "zig-out/bin/cart.wasm" rather than the full path. It falls back
// to the original whenever a relative form can't be computed or would escape
// the working directory (start with "..").
func displayPath(path string) string {
	cwd, err := os.Getwd()
	if err != nil {
		return path
	}

	rel, err := filepath.Rel(cwd, path)
	if err != nil || strings.HasPrefix(rel, "..") {
		return path
	}

	return rel
}

// serverURL builds the http URL to visit from a listen address, substituting a
// concrete host for the wildcard/empty ones a browser can't navigate to.
func serverURL(addr string) string {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return "http://localhost" + addr + "/"
	}

	switch host {
	case "", "0.0.0.0", "::", "[::]":
		host = "localhost"
	}

	return "http://" + net.JoinHostPort(host, port) + "/"
}

// openBrowser opens url in the system default browser.
func openBrowser(url string) error {
	switch runtime.GOOS {
	case "darwin":
		return exec.Command("open", url).Start()
	case "windows":
		return exec.Command("rundll32", "url.dll,FileProtocolHandler", url).Start()
	default:
		return exec.Command("xdg-open", url).Start()
	}
}

// serveBytes returns a handler that writes the given embedded bytes with the
// given Content-Type.
func serveBytes(b []byte, contentType string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", contentType)
		w.Header().Set("Cache-Control", "no-store")
		w.Write(b)
	}
}

// serveReload returns a Server-Sent Events handler that pushes a "reload"
// event whenever the cart file's modification time changes. The page listens
// on it so a rebuilt cart (e.g. from `zig build --watch`) live-reloads in the
// browser without a manual refresh. interval controls how often the cart is
// stat()ed for changes.
func serveReload(path string, interval time.Duration) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		flusher, ok := w.(http.Flusher)
		if !ok {
			http.Error(w, "streaming unsupported", http.StatusInternalServerError)
			return
		}

		w.Header().Set("Content-Type", "text/event-stream")
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Connection", "keep-alive")

		var last int64
		if fi, err := os.Stat(path); err == nil {
			last = fi.ModTime().UnixNano()
		}

		ticker := time.NewTicker(interval)
		defer ticker.Stop()

		for {
			select {
			case <-r.Context().Done():
				return
			case <-ticker.C:
				fi, err := os.Stat(path)
				if err != nil {
					continue
				}

				if m := fi.ModTime().UnixNano(); m != last {
					last = m
					fmt.Fprint(w, "data: reload\n\n")
					flusher.Flush()
				}
			}
		}
	}
}

// serveCart returns a handler that reads the cart from disk on each request, so
// rebuilding it and refreshing the page is enough to load the new bytes.
func serveCart(path string, stderr io.Writer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		b, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintf(stderr, "read %s: %v\n", displayPath(path), err)
			http.Error(w, "cart not found", http.StatusNotFound)
			return
		}

		w.Header().Set("Content-Type", "application/wasm")
		w.Header().Set("Cache-Control", "no-store")
		w.Write(b)
	}
}

// bundleFile is one entry written into a static bundle (both to disk and into
// the zip archive).
type bundleFile struct {
	name string
	data []byte
}

// writeBundle builds a self-contained static bundle that runs cart without a
// server: index.html, vex.js and the cart wasm are written to bundle/<name>/
// (where <name> is the cart's base name without extension) and the directory is
// also archived to bundle/<name>.zip.
func writeBundle(cart string, stdout io.Writer) error {
	wasm, err := os.ReadFile(cart)
	if err != nil {
		return fmt.Errorf("read cart: %w", err)
	}

	cartFile := filepath.Base(cart)
	name := strings.TrimSuffix(cartFile, filepath.Ext(cartFile))

	files := []bundleFile{
		{"index.html", bundleIndexHTML(cartFile)},
		{"vex.js", vexJS},
		{cartFile, wasm},
	}

	dir := filepath.Join("bundle", name)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}

	for _, f := range files {
		if err := os.WriteFile(filepath.Join(dir, f.name), f.data, 0o644); err != nil {
			return err
		}
	}

	zipPath := filepath.Join("bundle", name+".zip")
	if err := writeBundleZip(zipPath, name, files); err != nil {
		return err
	}

	fmt.Fprintf(stdout, "wrote bundle %s and %s\n", dir, zipPath)

	return nil
}

// bundleIndexHTML returns a static copy of the embedded index.html that loads
// cartFile directly, with the dev-only live-reload (SSE) script replaced so the
// page works straight from the filesystem with no server behind it.
func bundleIndexHTML(cartFile string) []byte {
	const (
		startTag = `<script type="module">`
		endTag   = "</script>"
	)

	html := string(indexHTML)

	i := strings.Index(html, startTag)
	if i < 0 {
		return indexHTML
	}

	j := strings.Index(html[i:], endTag)
	if j < 0 {
		return indexHTML
	}
	j += i + len(endTag)

	// Keep the gamepad bootstrap (orientation detection + setupGamepad()),
	// but drop the EventSource live-reload — there's no server in a static
	// bundle, so the SSE endpoint it pointed at doesn't exist.
	script := startTag + "\n" +
		`  import { start, setupGamepad } from "./vex.js";` + "\n\n" +
		`  function updateOrientation() {` + "\n" +
		`    document.body.classList.toggle("portrait",` + "\n" +
		`        window.innerHeight > window.innerWidth);` + "\n" +
		`  }` + "\n\n" +
		`  window.addEventListener("resize", updateOrientation);` + "\n" +
		`  window.addEventListener("orientationchange", updateOrientation);` + "\n" +
		`  updateOrientation();` + "\n\n" +
		`  setupGamepad();` + "\n\n" +
		`  window.addEventListener("load", () => start(` + strconv.Quote(cartFile) + "));\n" +
		endTag

	return []byte(html[:i] + script + html[j:])
}

// writeBundleZip archives files into the zip at path, placing every entry under
// a prefix/ directory so unzipping recreates the bundle folder.
func writeBundleZip(path, prefix string, files []bundleFile) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	zw := zip.NewWriter(f)

	for _, file := range files {
		w, err := zw.Create(prefix + "/" + file.name)
		if err != nil {
			return err
		}

		if _, err := w.Write(file.data); err != nil {
			return err
		}
	}

	return zw.Close()
}
