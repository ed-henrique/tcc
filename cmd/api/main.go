package main

import (
	"database/sql"
	"log"
	"net/http"
	"tcc/internal/database"

	"github.com/go-playground/validator/v10"
)

const port = "8080"

var (
  db *sql.DB
  validate *validator.Validate
)

func main() {
  db = database.New()
  validate = validator.New(validator.WithRequiredStructEnabled())

  mux := http.NewServeMux()
  mux.HandleFunc("/", home)
  mux.HandleFunc("/records", records)

  log.Println("Starting server on :" + port)

  err := http.ListenAndServe(":" + port, mux)
  log.Fatalln(err)
}
