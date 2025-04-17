package main

import (
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
)

func main() {

	port := "8080"
	if len(os.Args) < 2 {
		log.Fatal("Usage: ./chunk_reciver <port>")
	}
	if len(os.Args) == 2 {
		port = ":" + os.Args[1]
	}

	http.HandleFunc("/receive-chunk", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}

		data, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "Error reading body", http.StatusBadRequest)
			return
		}

		filename := fmt.Sprintf("chunk%s.txt", port)
		err = os.WriteFile(filename, data, 0644)
		if err != nil {
			http.Error(w, "Error writing file", http.StatusInternalServerError)
			return
		}

		w.WriteHeader(http.StatusOK)
	})

	fmt.Printf("Worker server starting on port %s\n", port)
	if err := http.ListenAndServe(port, nil); err != nil {
		fmt.Printf("Server failed to start: %v\n", err)
	}
}
