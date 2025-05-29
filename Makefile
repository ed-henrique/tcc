min_distance=600.0

mob:
	mkdir -p generated
	python3 "${SUMO_HOME}/tools/randomTrips.py" -n "sumo/grid.net.xml" -e $(end_time) -p $(period) --min-distance $(min_distance) -r "sumo/grid.rou.xml"
	sumo -c sumo/grid.sumocfg --fcd-output generated/trace.xml
	python3 "${SUMO_HOME}/tools/traceExporter.py" --fcd-input generated/trace.xml --ns2mobility-output generated/mobility.tcl
