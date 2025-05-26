mob:
	mkdir -p generated
	sumo -c sumo/grid.sumocfg --fcd-output generated/trace.xml
	python3 "${SUMO_HOME}/tools/traceExporter.py" --fcd-input generated/trace.xml --ns2mobility-output generated/mobility.tcl
