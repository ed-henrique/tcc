package models

import (
	"database/sql"
	"strconv"
)

type Records struct {
	Data []Record
}

type Record struct {
	Id      uint64 `json:"id"`
	Lat     float64 `json:"lat" validate:"required,latitude"`
	Lon     float64 `json:"lon" validate:"required,longitude"`
	Time    string `json:"time" validate:"required"`
	Name    string `json:"name"`
	Color   string `json:"color"`
	Vehicle uint64 `json:"vehicle" validate:"required,gt=0"`
}

func (r *Records) QueryLatest(db *sql.DB) error {
	rows, err := db.Query("SELECT record.id, record.lat, record.lon, record.time, record.vehicle, vehicle.name, vehicle.color FROM record INNER JOIN vehicle ON vehicle.id = record.vehicle ORDER BY record.time DESC LIMIT 5")
	defer rows.Close()

	if err != nil {
		return err
	}

	for rows.Next() {
		var record Record
		var latitude string
		var longitude string

		if err := rows.Scan(
			&record.Id,
			&latitude,
			&longitude,
			&record.Time,
			&record.Vehicle,
			&record.Name,
			&record.Color,
		); err != nil {
			return err
		}

		record.Lat, _ = strconv.ParseFloat(latitude, 64)
		record.Lon, _ = strconv.ParseFloat(longitude, 64)

		r.Data = append(r.Data, record)
	}

	return rows.Err()
}

func (r *Record) Insert(db *sql.DB) error {
	_, err := db.Exec(
		"INSERT INTO record (lat, lon, time, vehicle) VALUES ($1, $2, $3, $4)",
		r.Lat,
		r.Lon,
		r.Time,
		r.Vehicle,
	)

	return err
}
