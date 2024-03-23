CREATE TABLE IF NOT EXISTS record (
  id INTEGER PRIMARY KEY ASC,
  lat TEXT NOT NULL,
  lon TEXT NOT NULL,
  time DATETIME NOT NULL,
  vehicle INTEGER NOT NULL,
  FOREIGN KEY (vehicle) REFERENCES vehicle(id)
);

CREATE TABLE IF NOT EXISTS vehicle (
  id INTEGER PRIMARY KEY ASC,
  name TEXT NOT NULL,
  color TEXT NOT NULL
);