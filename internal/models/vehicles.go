package models

import "database/sql"

type Vehicles struct {
  Data []Vehicle
}

type Vehicle struct {
  Id uint64
  Name string
  Color string
}

func (v *Vehicles) QueryAll(db *sql.DB) error {
  rows, err := db.Query("SELECT id, name, color FROM vehicle ORDER BY name")
  defer rows.Close()

  if err != nil {
    return err
  }

  for rows.Next() {
    var vehicle Vehicle

    if err := rows.Scan(
      &vehicle.Id,
      &vehicle.Name,
      &vehicle.Color,
    ); err != nil {
      return err
    }

    v.Data = append(v.Data, vehicle)
  }

  return rows.Err()
}
