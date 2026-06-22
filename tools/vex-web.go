// Command vex-web is a small self-contained web server for the browser build
// of the vex fantasy console.
//
// Usage:
//
//	go run tools/vex-web.go [-addr host:port] <cart.wasm>
//
// index.html and vex.js are embedded into the binary (from tools/assets), so a
// built vex-web needs no files alongside it. The cart given as the first
// argument is served on /cart.wasm, read from disk on every request — so
// rebuilding the cart and refreshing the page picks up the new bytes without
// restarting the server.
//
// /cart.wasm is the default cart the page loads on start; drag-and-dropping a
// different .wasm onto the page still works and is handled entirely in the
// browser.
package main

import (
	_ "embed"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
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
}

func main() {
	if err := run(os.Args, os.Stderr); err != nil && !errors.Is(err, flag.ErrHelp) {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}

// run parses args, wires up the server and serves until it errors. It takes its
// arguments and output explicitly so it can be exercised without touching
// process globals.
func run(args []string, stderr io.Writer) error {
	var in Input

	fs := flag.NewFlagSet(args[0], flag.ContinueOnError)
	fs.SetOutput(stderr)

	fs.StringVar(&in.addr, "addr", ":8383", "address to listen on")
	fs.BoolVar(&in.noOpen, "no-open", false, "don't open the browser on start")
	fs.DurationVar(&in.poll, "poll", 500*time.Millisecond, "how often to stat the cart for live-reload")

	fs.Usage = func() {
		fmt.Fprintf(stderr,
			"usage: %s [-addr host:port] [--no-open] <cart.wasm>\n",
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

	// Warn early if the cart is missing, but keep serving: it may be (re)built
	// after the server starts.
	if _, err := os.Stat(cart); err != nil {
		log.Printf("warning: %v", err)
	}

	mux := http.NewServeMux()

	mux.HandleFunc("/vex.js", serveBytes(vexJS, "text/javascript; charset=utf-8"))
	mux.HandleFunc("/cart.wasm", serveCart(cart))
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
	ln, err := listen(in.addr, 5)
	if err != nil {
		return err
	}

	url := serverURL(ln.Addr().String())

	log.Printf("vex-web: serving cart %q on %s", cart, url)

	if !in.noOpen {
		if err := openBrowser(url); err != nil {
			log.Printf("could not open browser: %v (visit %s)", err, url)
		}
	}

	return http.Serve(ln, mux)
}

// listen binds a TCP listener to addr, retrying on the next port number (up to
// tries attempts) whenever the chosen port is already in use.
func listen(addr string, tries int) (net.Listener, error) {
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
			log.Printf("vex-web: port %d in use, trying %d", n+i, n+i+1)
		}
	}

	return nil, fmt.Errorf("no free port after %d attempts starting at %s", tries, addr)
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
func serveCart(path string) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		b, err := os.ReadFile(path)
		if err != nil {
			log.Printf("read %s: %v", path, err)
			http.Error(w, "cart not found", http.StatusNotFound)
			return
		}

		w.Header().Set("Content-Type", "application/wasm")
		w.Header().Set("Cache-Control", "no-store")
		w.Write(b)
	}
}
