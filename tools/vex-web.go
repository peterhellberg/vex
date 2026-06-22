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
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

//go:embed assets/index.html
var indexHTML []byte

//go:embed assets/vex.js
var vexJS []byte

func main() {
	addr := flag.String("addr", ":8383", "address to listen on")
	noOpen := flag.Bool("no-open", false, "don't open the browser on start")

	flag.Usage = func() {
		fmt.Fprintf(flag.CommandLine.Output(),
			"usage: %s [-addr host:port] [--no-open] <cart.wasm>\n",
			filepath.Base(os.Args[0]))
		flag.PrintDefaults()
	}

	flag.Parse()

	cart := flag.Arg(0)

	if cart == "" {
		flag.Usage()
		os.Exit(2)
	}

	// Warn early if the cart is missing, but keep serving: it may be (re)built
	// after the server starts.
	if _, err := os.Stat(cart); err != nil {
		log.Printf("warning: %v", err)
	}

	mux := http.NewServeMux()

	mux.HandleFunc("/vex.js", serveBytes(vexJS, "text/javascript; charset=utf-8"))
	mux.HandleFunc("/cart.wasm", serveCart(cart))

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" {
			http.NotFound(w, r)
			return
		}

		serveBytes(indexHTML, "text/html; charset=utf-8")(w, r)
	})

	// Bind the listener before opening the browser so the page never races
	// ahead of the server and hits a connection refused.
	ln, err := net.Listen("tcp", *addr)
	if err != nil {
		log.Fatal(err)
	}

	url := serverURL(*addr)

	log.Printf("vex-web: serving cart %q on %s", cart, url)

	if !*noOpen {
		if err := openBrowser(url); err != nil {
			log.Printf("could not open browser: %v (visit %s)", err, url)
		}
	}

	if err := http.Serve(ln, mux); err != nil {
		log.Fatal(err)
	}
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
