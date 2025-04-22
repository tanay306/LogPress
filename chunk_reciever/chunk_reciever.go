package main

import (
	"archive/zip"
	"bytes"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
)

// Compress and send files in output+port
func sendOutputFiles(w http.ResponseWriter, outputDir string) {
	var buf bytes.Buffer
	zipWriter := zip.NewWriter(&buf)

	err := filepath.Walk(outputDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() {
			return nil
		}

		relPath, err := filepath.Rel(outputDir, path)
		if err != nil {
			return err
		}

		file, err := os.Open(path)
		if err != nil {
			return err
		}
		defer file.Close()

		fw, err := zipWriter.Create(relPath)
		if err != nil {
			return err
		}

		_, err = io.Copy(fw, file)
		return err
	})

	if err != nil {
		http.Error(w, "Failed to create zip: "+err.Error(), http.StatusInternalServerError)
		return
	}

	if err := zipWriter.Close(); err != nil {
		http.Error(w, "Failed to finalize zip: "+err.Error(), http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "application/zip")

	w.Header().Set("Content-Disposition", "attachment; filename=\""+outputDir+".zip\"")
	w.WriteHeader(http.StatusOK)
	w.Write(buf.Bytes())
}

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
	var wg sync.WaitGroup

	http.HandleFunc("/receive-chunk", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}

		// Get sequence number from header
		seq := r.Header.Get("X-Sequence")
		if seq == "" {
			http.Error(w, "Missing sequence number", http.StatusBadRequest)
			println("Missing sequence number")
			return
		}

		origfilename := r.Header.Get("X-Filename")
		if origfilename == "" {
			http.Error(w, "Missing filename", http.StatusBadRequest)
			println("Missing filename")
			return
		}

		data, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "Error reading body", http.StatusBadRequest)
			println("Error reading body")
			return
		}

		wg.Add(1)

		// Save with sequence number
		filename := fmt.Sprintf("./chunks%s/%s_%s.txt", port, origfilename, seq)

		err = os.WriteFile(filename, data, 0644)
		if err != nil {
			http.Error(w, "Error writing file", http.StatusInternalServerError)
			println("Error writing file")
			return
		}
		println("wrote file:", filename)

		wg.Done()

		w.WriteHeader(http.StatusOK)
	})

	http.HandleFunc("/compress", func(w http.ResponseWriter, r *http.Request) {

		wg.Wait()

		fmt.Println("Starting compression...")
		cmd := exec.Command("./logpress", "compress1", "compressed_archive"+port+".mylp", "chunks"+port)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr

		fmt.Println("Making templates....")
		if err := cmd.Run(); err != nil {
			println("err", err.Error())
			w.WriteHeader(500)
			return
		}

		file, err := os.Open("variables.json")
		if err != nil {
			log.Fatalf("Error opening variables.json: %v", err)
		}
		defer file.Close()

		// Read the contents of the file into a byte slice using io.ReadAll
		fileContents, err := io.ReadAll(file)
		if err != nil {
			log.Fatalf("Error reading variables.json: %v", err)
		}

		// Create the HTTP POST request
		url := "http://dbserver:8083/upload" // Replace with your target URL
		req, err := http.NewRequest("POST", url, bytes.NewBuffer(fileContents))
		if err != nil {
			log.Fatalf("Error creating HTTP request: %v", err)
		}

		// Set the appropriate headers for sending JSON
		req.Header.Set("Content-Type", "application/json")

		// Send the request using the HTTP client
		client := &http.Client{}
		resp, err := client.Do(req)
		if err != nil {
			log.Fatalf("Error sending request: %v", err)
		}
		defer resp.Body.Close()

		// Handle the response
		if resp.StatusCode == http.StatusOK {
			fmt.Println("Successfully sent the JSON file!")

			// Read the response body (expecting JSON) using io.ReadAll
			responseBody, err := io.ReadAll(resp.Body)
			if err != nil {
				log.Fatalf("Error reading response body: %v", err)
			}

			// Save the response JSON to dictionaries.json using os.WriteFile
			err = os.WriteFile("dictionaries.json", responseBody, 0644)
			if err != nil {
				log.Fatalf("Error saving dictionaries.json: %v", err)
			}
			fmt.Println("Successfully saved response to dictionaries.json")
		} else {
			fmt.Printf("Failed to send the file. Status: %s\n", resp.Status)
		}

		cmd2 := exec.Command("./logpress", "compress2", "compressed_archive"+port+".mylp", "chunks"+port)
		cmd2.Stdout = os.Stdout
		cmd2.Stderr = os.Stderr

		if err := cmd2.Run(); err != nil {
			println("Err in cmd2:", err.Error())
			w.WriteHeader(500)
			return
		}

		dirPath := "chunks" + port
		files, err := os.ReadDir(dirPath)
		if err != nil {
			log.Fatalf("Error reading directory: %v", err)
		}

		for _, file := range files {
			if !file.IsDir() {
				err := os.RemoveAll(dirPath + "/" + file.Name())
				if err != nil {
					log.Fatalf("Error deleting file %s: %v", file.Name(), err)
				}
			} else {
				os.RemoveAll(dirPath + "/" + file.Name())
			}
		}

		w.WriteHeader(http.StatusOK)
	})

	http.HandleFunc("/decompress", func(w http.ResponseWriter, r *http.Request) {
		cmd := exec.Command("../logpress", "decompress", "compressed_archive"+port+".mylp", "output"+port)
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr

		fmt.Println("Starting decompression...")
		if err := cmd.Run(); err != nil {
			println("err", err.Error())
			w.WriteHeader(500)
			return
		}
		outputDir := "output" + port
		sendOutputFiles(w, outputDir)
		// w.WriteHeader(http.StatusOK)
		os.RemoveAll(outputDir)
	})

	http.HandleFunc("/search", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}
		// Read request body
		body, err := io.ReadAll(r.Body)
		if err != nil {
			if exitErr, ok := err.(*exec.ExitError); ok {
				http.Error(w, string(exitErr.Stderr), http.StatusInternalServerError)
			} else {
				http.Error(w, err.Error(), http.StatusInternalServerError)
			}
			return
		}
		defer r.Body.Close()

		query := string(body)

		print("search for query: ", query)

		cmd := exec.Command("../logpress", "search", "compressed_archive"+port+".mylp", query)

		output, err := cmd.Output()
		if err != nil {
			if exitErr, ok := err.(*exec.ExitError); ok {
				http.Error(w, string(exitErr.Stderr), http.StatusInternalServerError)
			} else {
				http.Error(w, err.Error(), http.StatusInternalServerError)
			}
			return
		}

		// Split and trim prefix from each line
		lines := strings.Split(string(output), "\n")
		var cleaned []string
		for _, line := range lines {
			if parts := strings.SplitN(line, ": ", 2); len(parts) == 2 {
				cleaned = append(cleaned, parts[1])
			}
		}
		cleanOutput := strings.Join(cleaned, "\n")

		// files, err := os.ReadDir(dirPath)
		// if err != nil {
		// 	log.Fatalf("Error reading directory: %v", err)
		// }

		// for _, file := range files {
		// 	if !file.IsDir() {
		// 		err := os.RemoveAll(dirPath + "/" + file.Name())
		// 		if err != nil {
		// 			log.Fatalf("Error deleting file %s: %v", file.Name(), err)
		// 		}
		// 	} else {
		// 		os.RemoveAll(dirPath + "/" + file.Name())
		// 	}
		// }

		// Return stdout as response
		w.WriteHeader(http.StatusOK)
		w.Write([]byte(cleanOutput))
	})

	fmt.Printf("Worker server starting on port %s\n", port)
	if err := http.ListenAndServe(port, nil); err != nil {
		fmt.Printf("Server failed to start: %v\n", err)
	}
}
