package database

import (
	"database/sql"
	"log"

	_ "modernc.org/sqlite"
)

const path = "local.db"

func New() *sql.DB {
	db, err := sql.Open("sqlite", path)

	if err != nil {
		log.Panicln(err)
	}

	return db
}
