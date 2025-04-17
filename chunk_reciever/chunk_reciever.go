package main

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
)

func main() {

	port := "8080"
	if len(os.Args) < 2 {
		log.Fatal("Usage: ./chunk_reciver <port>")
	}
	if len(os.Args) == 2 {
		port = ":" + os.Args[1]
	}

	dirPath := fmt.Sprintf("chunks%s", port)
	if _, err := os.Stat(dirPath); os.IsNotExist(err) {
		err := os.MkdirAll(dirPath, 0755)
		if err != nil {
			fmt.Println("Error creating directory:", err)
			return
		}
	}

	http.HandleFunc("/receive-chunk", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}

		// Get sequence number from header
		seq := r.Header.Get("X-Sequence")
		if seq == "" {
			http.Error(w, "Missing sequence number", http.StatusBadRequest)
			return
		}

		data, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "Error reading body", http.StatusBadRequest)
			return
		}

		// Save with sequence number
		filename := fmt.Sprintf("chunks%s/chunk_%s.txt", port, seq)
		err = os.WriteFile(filename, data, 0644)
		if err != nil {
			http.Error(w, "Error writing file", http.StatusInternalServerError)
			return
		}

		w.WriteHeader(http.StatusOK)
	})

	http.HandleFunc("/compress", func(w http.ResponseWriter, r *http.Request) {
		cmd := exec.Command("../logpress", "compress", "compressed_archive"+port+".mylp", "chunks"+port)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr

		fmt.Println("Starting compression...")
		if err := cmd.Run(); err != nil {
			println("err", err.Error())
			w.WriteHeader(500)
			return
		}

		w.WriteHeader(http.StatusOK)
	})

	fmt.Printf("Worker server starting on port %s\n", port)
	if err := http.ListenAndServe(port, nil); err != nil {
		fmt.Printf("Server failed to start: %v\n", err)
	}
}
