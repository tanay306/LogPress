package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"

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

		insertMany(tx, "templates", payload.Templates)
		insertMany(tx, "variables", payload.Variables)
		insertMany(tx, "files", payload.Files)

		if err := tx.Commit(); err != nil {
			http.Error(w, "Failed to commit transaction", http.StatusInternalServerError)
			return
		}

		fmt.Fprintln(w, "Inserted JSON data into SQLite tables successfully.")
	})

	fmt.Println("Server started at :8080")
	log.Fatal(http.ListenAndServe(":8080", nil))
}

func createTables(db *sql.DB) {
	stmts := []string{
		`CREATE TABLE IF NOT EXISTS templates (id INTEGER PRIMARY KEY AUTOINCREMENT, template TEXT);`,
		`CREATE TABLE IF NOT EXISTS variables (id INTEGER PRIMARY KEY AUTOINCREMENT, value TEXT);`,
		`CREATE TABLE IF NOT EXISTS files (id INTEGER PRIMARY KEY AUTOINCREMENT, filename TEXT);`,
	}

	for _, stmt := range stmts {
		if _, err := db.Exec(stmt); err != nil {
			log.Fatalf("Error creating table: %v", err)
		}
	}
}

func insertMany(tx *sql.Tx, table string, items []string) {
	stmt, err := tx.Prepare(fmt.Sprintf("INSERT INTO %s (%s) VALUES (?);", table, columnName(table)))
	if err != nil {
		log.Fatalf("Prepare failed for table %s: %v", table, err)
	}
	defer stmt.Close()

	for _, item := range items {
		if _, err := stmt.Exec(item); err != nil {
			log.Fatalf("Insert failed for table %s: %v", table, err)
		}
	}
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
