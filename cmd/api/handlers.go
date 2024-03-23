package main

import (
	"encoding/json"
	"html/template"
	"log"
	"net/http"
	"tcc/internal/models"
)

func home(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.Redirect(w, r, "/", http.StatusPermanentRedirect)
		return
	}

	var records models.Records

	err := records.QueryLatest(db)

	if err != nil {
		log.Println(err)
		http.Error(w, "A problem occurred when querying the db", http.StatusInternalServerError)
		return
	}

	tmpl := template.Must(template.New("index").Parse(`
  <!DOCTYPE html>
  <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <meta http-equiv="X-UA-Compatible" content="ie=edge">
      <title>Registros</title>
      <script src="https://cdn.tailwindcss.com"></script>
      <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"
	integrity="sha256-p4NxAoJBhIIN+hmNHrzRCf9tD/miZyoHS5obTRR9BMY="
	crossorigin=""/>
      <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"
	integrity="sha256-20nQCchB9co0qIjJZRGuk2/Z9VM+kNiyxNV1lvTlZBo="
	crossorigin=""></script>
    </head>
    <body>
    <div class="bg-slate-50 min-h-screen p-10 md:h-screen ">
	<main class="flex flex-col gap-10 h-full md:grid md:grid-cols-4">
	<article class="border-purple-500 h-[500px] overflow-hidden rounded-md md:col-span-3 md:h-full">
	    <div id="map" class="h-full"></div>
	  </article>
	  <article class="border divide-y h-full p-6 rounded-md shadow-md">
	    {{ range .Data }}
	      <section class="block p-3">
					<div class="flex items-center justify-between">
						<h3 class="font-semibold text-slate-900 text-lg">{{ .Name }}</h3>
						<span class="font-light text-slate-500 text-sm">{{ slice .Time 8 10 }}/{{ slice .Time 5 7 }}/{{ slice .Time 2 4 }} {{ slice .Time 11 16 }}</span>
					</div>
					<div class="flex font-light gap-1 items-center justify-end text-slate-500 text-sm">
						<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" class="h-4 w-4">
							<path fill-rule="evenodd" d="m7.539 14.841.003.003.002.002a.755.755 0 0 0 .912 0l.002-.002.003-.003.012-.009a5.57 5.57 0 0 0 .19-.153 15.588 15.588 0 0 0 2.046-2.082c1.101-1.362 2.291-3.342 2.291-5.597A5 5 0 0 0 3 7c0 2.255 1.19 4.235 2.292 5.597a15.591 15.591 0 0 0 2.046 2.082 8.916 8.916 0 0 0 .189.153l.012.01ZM8 8.5a1.5 1.5 0 1 0 0-3 1.5 1.5 0 0 0 0 3Z" clip-rule="evenodd" />
						</svg>
						<span>({{ printf "%.6f" .Lat }}, {{ printf "%.6f" .Lon }})</span>
					</div>
	      </section>
	    {{ end }}
	  </article>
	</main>
      </div>
    </body>
    <script>
      const map = L.map('map').setView({ lat: 2.81954, lng: -60.6714 }, 14);

      L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
	  maxZoom: 19,
	  attribution: '&copy; <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
      }).addTo(map);

      {{ range .Data }}
				const polyline = L.polyline([[2.81954, -60.6714], [2.8196, -60.672]], { color: "red" }).addTo(map);
      {{ end }}

      map.fitBounds(polyline.getBounds());
    </script>
  </html>
  `))

	err = tmpl.Execute(w, records)

	if err != nil {
		log.Println(err)
		http.Error(w, "A problem occurred when rendering the template", http.StatusInternalServerError)
		return
	}
}

func records(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Add("Allow", http.MethodPost)
		http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
		return
	}

	var record models.Record

	defer r.Body.Close()
	err := json.NewDecoder(r.Body).Decode(&record)

	if err != nil {
		log.Println(err)
		http.Error(w, "A problem occurred when reading from body", http.StatusBadRequest)
		return
	}

	err = validate.Struct(record)

	if err != nil {
		log.Println(err)
		http.Error(w, "A problem occurred when validating the body", http.StatusBadRequest)
		return
	}

	err = record.Insert(db)

	if err != nil {
		log.Println(err)
		http.Error(w, "A problem occurred when inserting to db", http.StatusInternalServerError)
		return
	}

	w.WriteHeader(http.StatusCreated)
	w.Write([]byte("Record inserted successfully"))
}
