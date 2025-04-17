package main

import (
	"bufio"
	"log"
	"net/http"
	"os"
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

	// print("operation", operation)

	if operation == "compress" {

		if len(os.Args) < 3 {
			log.Fatal("Usage: ./chunk_sender compression <filename> <port1> <port2> ...")
		}

		fileName := os.Args[2]

		workers := []string{}

		for port := 3; port < len(os.Args); port++ {
			workers = append(workers, "http://localhost:"+os.Args[port])
		}

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
				sequence++

				go func(data, target string, seq int) {
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

					resp, err := http.DefaultClient.Do(req)
					if err != nil {
						log.Printf("Error sending chunk %d: %v", seq, err)
						return
					}
					defer resp.Body.Close()
					if resp.StatusCode != http.StatusOK {
						log.Printf("Non-OK response for chunk %d: %d", seq, resp.StatusCode)
					}
				}(data, target, currentSeq)

				workerIndex++
				chunk = make([]string, 0, chunkSize)
			}
		}

		// Handle remaining lines
		if len(chunk) > 0 {
			data := strings.Join(chunk, "\n")
			target := workers[workerIndex%len(workers)]
			currentSeq := sequence

			req, err := http.NewRequest("POST", target, strings.NewReader(data))
			if err != nil {
				log.Fatal(err)
			}
			req.Header.Set("X-Sequence", strconv.Itoa(currentSeq))

			resp, err := http.DefaultClient.Do(req)
			if err != nil {
				log.Printf("Error sending final chunk: %v", err)
			} else {
				defer resp.Body.Close()
				if resp.StatusCode != http.StatusOK {
					log.Printf("Non-OK final response: %d", resp.StatusCode)
				}
			}
			sequence++
		}

		if err := scanner.Err(); err != nil {
			log.Fatal("Error reading file:", err)
		}

		wg.Wait()

		var wg2 sync.WaitGroup
		successChan := make(chan bool, len(workers))

		for _, url := range workers {
			wg2.Add(1)
			go func(url string) {
				defer wg2.Done()
				// for {
				resp, err := http.Get(url + "/compress")
				if err == nil && resp.StatusCode == http.StatusOK {
					successChan <- true
					return
				}
				// You can add a small delay here if you want to avoid aggressive retries
				// }
				println("failed compression for" + url)
			}(url)
		}

		// Wait until all requests have returned 200 OK
		wg2.Wait()
		close(successChan)

		println("compressed file", fileName)

	}
}
