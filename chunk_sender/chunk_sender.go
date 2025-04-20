package main

import (
	"archive/zip"
	"bufio"
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
)

const (
	chunkSize     = 200
	maxConcurrent = 100
)

func main() {
	if len(os.Args) < 2 {
		log.Fatal("Usage: ./chunk_sender <operation> ...")
	}

	operation := os.Args[1]

	data, err := ioutil.ReadFile("settings.json")
	if err != nil {
		fmt.Println("Error reading file:", err)
		return
	}

	var workerMap map[string]string
	if err := json.Unmarshal(data, &workerMap); err != nil {
		fmt.Println("Error unmarshaling JSON:", err)
		return
	}

	// Convert map values to slice
	var workers []string
	for _, url := range workerMap {
		workers = append(workers, url)
	}

	if operation == "compress" {

		if len(os.Args) < 3 {
			log.Fatal("Usage: ./chunk_sender compression <filename> <port1> <port2> ...")
		}

		fileName := os.Args[2]

		file, err := os.Open(fileName)
		if err != nil {
			log.Fatal(err)
		}
		defer file.Close()

		scanner := bufio.NewScanner(file)
		sem := make(chan struct{}, maxConcurrent)
		var wg sync.WaitGroup

		workerIndex := 0
		chunk := make([]string, 0, chunkSize)
		sequence := 0

		for scanner.Scan() {
			chunk = append(chunk, scanner.Text())
			if len(chunk) == chunkSize {
				wg.Add(1)
				sem <- struct{}{}

				data := strings.Join(chunk, "\n")
				target := workers[workerIndex%len(workers)] + "/receive-chunk"
				currentSeq := sequence
				currFilename := fileName
				sequence++

				go func(data, target string, seq int, fileName string) {
					defer func() {
						<-sem
						wg.Done()
					}()

					req, err := http.NewRequest("POST", target, strings.NewReader(data))
					if err != nil {
						log.Printf("Error creating request: %v", err)
						return
					}
					req.Header.Set("X-Sequence", strconv.Itoa(seq))
					req.Header.Set("X-Filename", fileName)

					resp, err := http.DefaultClient.Do(req)
					if err != nil {
						log.Printf("Error sending chunk %d: %v", seq, err)
						return
					}
					defer resp.Body.Close()
					if resp.StatusCode != http.StatusOK {
						log.Printf("Non-OK response for chunk %d: %d", seq, resp.StatusCode)
					}
				}(data, target, currentSeq, currFilename)

				workerIndex++
				chunk = make([]string, 0, chunkSize)
			}
		}

		// Handle remaining lines
		if len(chunk) > 0 {
			data := strings.Join(chunk, "\n")
			target := workers[workerIndex%len(workers)]
			currentSeq := sequence

			println("last sequence", currentSeq)

			req, err := http.NewRequest("POST", target, strings.NewReader(data))
			if err != nil {
				log.Fatal(err)
			}
			req.Header.Set("X-Sequence", strconv.Itoa(currentSeq))
			req.Header.Set("X-Filename", fileName)

			resp, err := http.DefaultClient.Do(req)
			if err != nil {
				// log.Printf("Error sending final chunk: %v", err)
			} else {
				defer resp.Body.Close()
				if resp.StatusCode != http.StatusOK {
					// log.Printf("Non-OK final response: %d", resp.StatusCode)
				}
			}
			sequence++
		}

		if err := scanner.Err(); err != nil {
			log.Fatal("Error reading file:", err)
		}

		wg.Wait()

		println("Upload completed")

		var wg2 sync.WaitGroup
		successChan := make(chan bool, len(workers))

		for _, url := range workers {
			wg2.Add(1)
			go func(url string) {
				defer wg2.Done()
				resp, err := http.Get(url + "/compress")
				if err == nil && resp.StatusCode == http.StatusOK {
					successChan <- true
					return
				}
				println("failed compression for " + url)
			}(url)
		}

		// Wait until all requests have returned 200 OK
		wg2.Wait()
		close(successChan)

		println("compressed file", fileName)
	} else if operation == "decompress" {
		var wg2 sync.WaitGroup
		successChan := make(chan bool, len(workers))
		var zipNames []string
		var zipNamesMu sync.Mutex // mutex for safe concurrent access

		println("Doing decompression")

		for _, url := range workers {
			wg2.Add(1)

			go func(url string) {
				defer wg2.Done()
				resp, err := http.Get(url + "/decompress")
				if err != nil {
					println("failed compression for " + url + ": " + err.Error())
					return
				}
				defer resp.Body.Close()

				if resp.StatusCode != http.StatusOK {
					println("non-200 response from " + url)
					return
				}
				bodyData, err := io.ReadAll(resp.Body)
				if err != nil {
					log.Println("Failed to read body:", err)
					return
				}

				contentDisp := resp.Header.Get("Content-Disposition")
				_, params, err := mime.ParseMediaType(contentDisp)
				var zipName string
				if err == nil {
					zipName = strings.TrimSuffix(params["filename"], ".zip")
				}
				if zipName == "" {
					zipName = "output_from_" + strings.ReplaceAll(url, "http://localhost:", "")
				}

				// Append to zipNames list safely
				zipNamesMu.Lock()
				zipNames = append(zipNames, zipName)
				zipNamesMu.Unlock()

				// Unzip from the body
				readerAt := bytes.NewReader(bodyData)
				zipReader, err := zip.NewReader(readerAt, int64(len(bodyData)))
				if err != nil {
					log.Println("Failed to read zip from response:", err)
					return
				}

				// Create the output directory
				outputDir := filepath.Join(".", zipName)
				os.MkdirAll(outputDir, 0755)

				// Extract files
				for _, f := range zipReader.File {
					destPath := filepath.Join(outputDir, f.Name)

					// Ensure path is safe
					if !strings.HasPrefix(destPath, filepath.Clean(outputDir)+string(os.PathSeparator)) {
						log.Println("Illegal file path in zip:", f.Name)
						continue
					}

					if f.FileInfo().IsDir() {
						os.MkdirAll(destPath, 0755)
						continue
					}

					srcFile, err := f.Open()
					if err != nil {
						log.Println("Failed to open zip entry:", err)
						continue
					}
					defer srcFile.Close()

					if err := os.MkdirAll(filepath.Dir(destPath), 0755); err != nil {
						log.Println("Failed to create directory:", err)
						continue
					}

					destFile, err := os.Create(destPath)
					if err != nil {
						log.Println("Failed to create file:", err)
						continue
					}

					_, err = io.Copy(destFile, srcFile)
					destFile.Close()
					if err != nil {
						log.Println("Failed to write file:", err)
						continue
					}
				}

				log.Println("Successfully decompressed data from", url, "into", outputDir)

				successChan <- true
			}(url)
		}

		wg2.Wait()

		type filePart struct {
			path   string
			number int
		}

		// Map to hold base name -> list of matching files
		fileGroups := make(map[string][]filePart)

		re := regexp.MustCompile(`^(.*)_(\d+)\.txt$`)

		err := filepath.WalkDir(".", func(path string, d os.DirEntry, err error) error {
			if err != nil {
				return err
			}

			if d.IsDir() {
				return nil
			}

			filename := filepath.Base(path)

			// Match pattern and extract base name
			if matches := re.FindStringSubmatch(filename); matches != nil {
				base := matches[1]
				numStr := matches[2]

				number, err := strconv.Atoi(numStr)
				if err != nil {
					log.Println("Failed to parse number from:", filename)
					return nil
				}

				fileGroups[base] = append(fileGroups[base], filePart{path, number})
			}

			return nil
		})

		if err != nil {
			log.Fatal(err)
		}

		outputDir := "decompressed"
		os.MkdirAll(outputDir, 0755) // Ensure the output directory exists

		// Step 2: Sort and append
		for base, parts := range fileGroups {
			sort.Slice(parts, func(i, j int) bool {
				return parts[i].number < parts[j].number
			})

			outputPath := filepath.Join(outputDir, base)
			outFile, err := os.Create(outputPath)
			if err != nil {
				log.Printf("Failed to create output file %s.txt: %v\n", base, err)
				continue
			}
			defer outFile.Close()

			fmt.Printf("Writing %s.txt:\n", base)

			for _, part := range parts {
				fmt.Println("  -", part.path)
				inFile, err := os.Open(part.path)
				if err != nil {
					log.Printf("Failed to open %s: %v\n", part.path, err)
					continue
				}

				_, err = io.Copy(outFile, inFile)
				if err != nil {
					log.Printf("Failed to copy from %s: %v\n", part.path, err)
				}
				inFile.Close()
			}
		}

	} else {

		if len(os.Args) < 3 {
			log.Fatal("Usage: ./chunk_sender search query")
		}

		searchquery := os.Args[2]

		var wg2 sync.WaitGroup
		var mu sync.Mutex
		outputFile, err := os.OpenFile("search_result.txt", os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			log.Fatalf("Failed to open output file: %v", err)
		}
		defer outputFile.Close()

		successChan := make(chan bool, len(workers))
		println("Searching")
		for _, url := range workers {
			wg2.Add(1)

			go func(url string, searchquery string) {
				defer wg2.Done()
				req, err := http.NewRequest("POST", url+"/search", strings.NewReader(searchquery))
				if err != nil {
					log.Printf("Error creating request: %v", err)
					return
				}

				resp, err := http.DefaultClient.Do(req)
				if err == nil && resp.StatusCode == http.StatusOK {
					body, err := io.ReadAll(resp.Body)
					if err != nil {
						log.Printf("Failed to read response body from %s: %v", url, err)
						return
					}

					mu.Lock()
					_, err = outputFile.Write(body)
					if err == nil {
						_, _ = outputFile.WriteString("\n") // Add newline after each response
					}
					mu.Unlock()

					if err != nil {
						log.Printf("Failed to write response to file: %v", err)
					} else {
						successChan <- true
					}
					return
				}
				println("search for " + url)
			}(url, searchquery)
		}

		wg2.Wait()

	}
}
