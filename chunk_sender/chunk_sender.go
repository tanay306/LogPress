package main

import (
	"bufio"
	"log"
	"net/http"
	"os"
	"strings"
	"sync"
)

const (
	chunkSize     = 1000
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
			workers = append(workers, "http://localhost:"+os.Args[port]+"/receive-chunk")
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

		for scanner.Scan() {
			chunk = append(chunk, scanner.Text())
			if len(chunk) == chunkSize {
				wg.Add(1)
				sem <- struct{}{}

				data := strings.Join(chunk, "\n")
				target := workers[workerIndex%len(workers)]
				workerIndex++

				go func(data, target string) {
					defer func() {
						<-sem
						wg.Done()
					}()

					resp, err := http.Post(target, "text/plain", strings.NewReader(data))
					if err != nil {
						log.Printf("Error sending to %s: %v", target, err)
						return
					}
					defer resp.Body.Close()
					if resp.StatusCode != http.StatusOK {
						log.Printf("Non-OK response from %s: %d", target, resp.StatusCode)
					}
				}(data, target)

				chunk = make([]string, 0, chunkSize)
			}
		}

		if len(chunk) > 0 {
			data := strings.Join(chunk, "\n")
			target := workers[workerIndex%len(workers)]

			resp, err := http.Post(target, "text/plain", strings.NewReader(data))
			if err != nil {
				log.Printf("Error sending final chunk to %s: %v", target, err)
			} else {
				defer resp.Body.Close()
				if resp.StatusCode != http.StatusOK {
					log.Printf("Non-OK final response from %s: %d", target, resp.StatusCode)
				}
			}
		}

		if err := scanner.Err(); err != nil {
			log.Fatal("Error reading file:", err)
		}

		wg.Wait()

		println("compressed file", fileName)

	}
}
