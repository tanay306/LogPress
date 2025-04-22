package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"

	_ "github.com/mattn/go-sqlite3"
)

// Struct to represent incoming JSON
type Payload struct {
	Templates []string `json:"templates"`
	Variables []string `json:"variables"`
	Files     []string `json:"files"`
}

func main() {
	db, err := sql.Open("sqlite3", "./logdata.db")
	if err != nil {
		log.Fatal(err)
	}
	defer db.Close()

	// Create tables
	createTables(db)

	http.HandleFunc("/upload", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "Only POST supported", http.StatusMethodNotAllowed)
			return
		}

		println("Got request to upload")

		body, err := io.ReadAll(r.Body)
		if err != nil {
			http.Error(w, "Error reading request", http.StatusBadRequest)
			return
		}
		defer r.Body.Close()

		var payload Payload
		if err := json.Unmarshal(body, &payload); err != nil {
			http.Error(w, "Invalid JSON", http.StatusBadRequest)
			return
		}

		tx, err := db.Begin()
		if err != nil {
			http.Error(w, "Failed to begin transaction", http.StatusInternalServerError)
			return
		}
		idMap := make(map[string]map[string]int64)
		insertMany(tx, "templates", payload.Templates, idMap)
		insertMany(tx, "variables", payload.Variables, idMap)
		insertMany(tx, "files", payload.Files, idMap)

		if err := tx.Commit(); err != nil {
			http.Error(w, "Failed to commit transaction", http.StatusInternalServerError)
			println("Failed to commit transaction: %s", err.Error())
			return
		}

		println("Finished updating database")

		err = getDictionary(db, idMap)
		if err != nil {
			http.Error(w, "Failed to get dictionary", http.StatusInternalServerError)
			println("Failed to get dictionary: %s", err.Error())
			return
		}

		response, err := json.Marshal(idMap)
		if err != nil {
			http.Error(w, "Failed to marshal response", http.StatusInternalServerError)
			println("Failed to marshal response: %s", err.Error())
			return
		}

		f, err := os.Create("global_dictionary.json")
		if err != nil {
			println("Failed to create idmap response file: %s", err.Error())
			http.Error(w, "Failed to create file", http.StatusInternalServerError)
			return
		}
		println("Done writing the global_dictionary.json")
		defer f.Close()

		// 2. Write the JSON bytes to the file
		if _, err := f.Write(response); err != nil {
			println("Failed to  write response to file: %s", err.Error())
			http.Error(w, "Failed to write response to file"+err.Error(), http.StatusInternalServerError)
			return
		}

		// Set the content type to JSON and write the response
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write(response)

		filePath := "./logdata.db"

		fileInfo, err := os.Stat(filePath)
		if err != nil {
			fmt.Println("Error:", err)
			return
		}

		fileSize := fileInfo.Size()
		sizeMB := float64(fileSize) / (1024.0 * 1024.0)
		fmt.Printf("Database final size: %.2f MB\n", sizeMB)

	})

	http.HandleFunc("/getGlobalDictionary", func(w http.ResponseWriter, r *http.Request) {
		println("Got request")

		idMap := make(map[string]map[string]int64)

		println("Finished updating database")

		err = getDictionary(db, idMap)
		if err != nil {
			http.Error(w, "Failed to get dictionary", http.StatusInternalServerError)
			println("Failed to get dictionary: %s", err.Error())
			return
		}

		response, err := json.Marshal(idMap)
		if err != nil {
			http.Error(w, "Failed to marshal response", http.StatusInternalServerError)
			println("Failed to marshal response: %s", err.Error())
			return
		}

		f, err := os.Create("global_dictionary.json")
		if err != nil {
			println("Failed to create idmap response file: %s", err.Error())
			http.Error(w, "Failed to create file", http.StatusInternalServerError)
			return
		}
		println("Done writing the global_dictionary.json")
		defer f.Close()

		// 2. Write the JSON bytes to the file
		if _, err := f.Write(response); err != nil {
			println("Failed to  write response to file: %s", err.Error())
			http.Error(w, "Failed to write response to file"+err.Error(), http.StatusInternalServerError)
			return
		}

		println("Sent the request global_dictionary.json.")

		// Set the content type to JSON and write the response
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(http.StatusOK)
		w.Write(response)
	})

	fmt.Println("Database Server started at 8083")
	log.Fatal(http.ListenAndServe(":8083", nil))
}

func createTables(db *sql.DB) {
	stmts := []string{
		`CREATE TABLE IF NOT EXISTS templates (id INTEGER PRIMARY KEY AUTOINCREMENT, template TEXT UNIQUE);`,
		`CREATE TABLE IF NOT EXISTS variables (id INTEGER PRIMARY KEY AUTOINCREMENT, value TEXT UNIQUE);`,
		`CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY AUTOINCREMENT, filename TEXT UNIQUE);`,
	}

	for _, stmt := range stmts {
		if _, err := db.Exec(stmt); err != nil {
			log.Fatalf("Error creating table: %v", err)
		}
	}
}

func getDictionary(db *sql.DB, idMap map[string]map[string]int64) error {
	// define for each table which text column to pull
	tables := map[string]string{
		"templates": "template",
		"variables": "value",
		"files":     "filename",
	}

	for tbl, col := range tables {
		// init the inner map for this table
		idMap[tbl] = make(map[string]int64)

		// query id + text-column
		query := fmt.Sprintf("SELECT id, %s FROM %s;", col, tbl)
		rows, err := db.Query(query)
		if err != nil {
			return fmt.Errorf("query %s: %w", tbl, err)
		}
		defer rows.Close()

		// scan into the map
		for rows.Next() {
			var (
				id  int64
				val string
			)
			if err := rows.Scan(&id, &val); err != nil {
				return fmt.Errorf("scan %s: %w", tbl, err)
			}
			idMap[tbl][val] = id
		}
		if err := rows.Err(); err != nil {
			return fmt.Errorf("rows %s: %w", tbl, err)
		}
	}
	return nil
}

func insertMany(tx *sql.Tx, table string, items []string, idMap map[string]map[string]int64) error {
	// Initialize the map for the specific table if it doesn't exist
	// if _, exists := idMap[table]; !exists {
	// 	idMap[table] = make(map[string]int64)
	// }

	// Prepare the statement
	stmt, err := tx.Prepare(fmt.Sprintf("INSERT OR IGNORE INTO %s (%s) VALUES (?);", table, columnName(table)))
	if err != nil {
		log.Fatalf("Prepare failed for table %s: %v", table, err)
		return err
	}
	defer stmt.Close()

	for _, item := range items {
		// Execute the insert statement
		_, err := stmt.Exec(item)
		if err != nil {
			log.Fatalf("Insert failed for table %s: %v", table, err)
			return err
		}

		// Get the ID of the inserted row
		// lastInsertID, err := result.LastInsertId()
		// if err != nil {
		// 	log.Fatalf("Failed to get last insert ID: %v", err)
		// 	return err
		// }

		// Save the value and corresponding row ID in the map for the table
		// idMap[table][item] = lastInsertID
	}

	return nil
}

func columnName(table string) string {
	switch table {
	case "templates":
		return "template"
	case "variables":
		return "value"
	case "files":
		return "filename"
	default:
		return "value"
	}
}
