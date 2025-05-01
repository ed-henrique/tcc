#!/usr/bin/python3
import subprocess
from time import sleep
from threading import Thread
import queue
import time


# 1. Comando para executar quando acabar de configurar o ambiente de desenvolvimento
#   configure_command = "cd ../../ && ./waf clean && CXXFLAGS=\"-O3 -w\" ./waf -d optimized configure --enable-examples --enable-modules=lte --disable-python"
#
# 2. Outros comandos que já constavam no script original
#   simulation_command = "cd ../../ && ./waf --run \"lena-nb-5G-scenario"
#   callgrind_command = "cd ../../ && ./waf --command-template\"valgrind --tool=callgrind  \%\s\" --run \"lena-nb-udp-data-transfer"

# Comando optimizado
sim_command = "./build/src/lte/examples/ns3.32-lena-nb-5G-scenario-optimized"

# Comando de depuração (não consegui fazer funcionar ainda)
# sim_command = "cd ../../ && ./build/src/lte/examples/ns3.32-lena-nb-5G-scenario-debug"


class SimulationParameters:
    def __init__(
        self,
        simulation,
        sim_time,
        sim_name,
        random_seed,
        coverage,
        payload_size,
        sync_frequency,
        num_ues,
        edt,
        lib_path="./build/lib",
    ):
        self.simulation = simulation  # .cc file to execute
        self.sim_time = sim_time  # in MilliSeconds
        self.sim_name = sim_name
        self.random_seed = random_seed  # For Random Number Generator
        self.coverage = coverage  # in Percentage
        self.payload_size = payload_size  # in Bytes
        self.sync_frequency = sync_frequency  # in Seconds
        self.num_ues = num_ues  # Number of UEs for the application / use cases
        self.edt = edt  # If Early Data Transmission should be used
        self.lib_path = (
            lib_path  # Use this path instead of whatever LD_LIBRARY_PATH holds
        )

    def getLibPath(self):
        return self.lib_path

    def generateExecutableCall(self):
        call = self.simulation
        call += f" --simName={self.simName}"
        call += f" --simTime={self.simTime}"
        call += f" --randomSeed={self.randomSeed}"
        call += f" --coverage={self.coverage}"
        call += f" --payloadSize={self.payload_size}"
        call += f" --syncFrequency={self.sync_frequency}"
        call += f" --numUes={self.num_ues}"
        call += f" --edt={self.edt}"
        return call


class TaskQueue(queue.Queue):
    def __init__(self, num_workers=1):
        queue.Queue.__init__(self)
        self.num_workers = num_workers

    def add_task(self, task, *args, **kwargs):
        args = args or ()
        kwargs = kwargs or {}
        self.put((task, args, kwargs))

    def start_workers(self):
        for i in range(self.num_workers):
            t = Thread(target=self.worker, args=(i,))
            t.daemon = True
            t.start()

    def worker(self, id):
        while True:
            sleep(5)
            simulationParameters = self.get()
            print(simulationParameters)
            cmd = simulationParameters[0].generateExecutableCall()
            cmd += f" --worker={id}"
            print(cmd)
            try:
                result = subprocess.run(
                    cmd,
                    shell=True,
                    check=True,
                    env={"LD_LIBRARY_PATH": simulationParameters[0].getLibPath()},
                )
                if result.returncode != 0:
                    # Task failed, maybe because of missing resources
                    # put simulation back on stac
                    print(
                        f"Something failed! Return code: {result.returncode}. Output: {result.stdout}. Error: {result.stderr}. Original cmd: {cmd}"
                    )
                    self.put(simulationParameters)
            except:
                # Task failed, maybe because of missing resources
                # put simulation back on stac
                print(
                    f"Something failed due to unknown exception... Original cmd: {cmd}"
                )
                self.put(simulationParameters)

            self.task_done()


start_time = time.time()
simTime = 12 * 60 * 60  # Simulation time in seconds
simu_queue = TaskQueue(
    7
)  # This is the number of parallel workers. This number should be below your number of CPU cores. Note that more parallel workers consume more RAM
seed = (
    33  # Number of runs. 5 means that simulations with seeds 1,2,3,4,5 will be started
)
for i in range(1, seed + 1):
    # Default
    simu_queue.add_task(
        SimulationParameters(
            simName="Default",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=40,
            num_ues=100,
            payload_size=200,
            sync_frequency=1,
            edt=True,
        )
    )

    # Quantidade de Nós
    simu_queue.add_task(
        SimulationParameters(
            simName="Node amount",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=40,
            num_ues=1000,
            payload_size=200,
            sync_frequency=1,
            edt=True,
        )
    )

    # Cobertura de Rede
    simu_queue.add_task(
        SimulationParameters(
            simName="Coverage",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=20,
            num_ues=100,
            payload_size=200,
            sync_frequency=1,
            edt=True,
        )
    )

    # Tamanho do Payload
    simu_queue.add_task(
        SimulationParameters(
            simName="Payload Size",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=40,
            num_ues=100,
            payload_size=400,
            sync_frequency=1,
            edt=True,
        )
    )

    # Modo de Transmissão
    simu_queue.add_task(
        SimulationParameters(
            simName="Transmission Mode",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=40,
            num_ues=100,
            payload_size=200,
            sync_frequency=1,
            edt=False,
        )
    )

    # Frequência de Coleta de Dados de Rastreamento
    simu_queue.add_task(
        SimulationParameters(
            simName="Sync Frequency",
            simTime=simTime,
            simulation=sim_command,
            randomSeed=i,
            coverage=40,
            num_ues=100,
            payload_size=200,
            sync_frequency=10,
            edt=True,
        )
    )
simu_queue.start_workers()

simu_queue.join()
print("--- %s seconds ---" % (time.time() - start_time))
