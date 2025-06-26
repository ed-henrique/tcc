#!/usr/bin/python3
import os
import subprocess
from time import sleep
from threading import Thread
import queue
import time

# 1. Comando para executar quando acabar de configurar o ambiente de desenvolvimento
#   configure_command = "cd ../../ && ./waf clean && CXXFLAGS='-O3 -w' ./waf -d optimized configure --enable-examples --enable-modules=lte --disable-python"

# Comando optimizado
# sim_command = "./build/src/lte/examples/ns3.32-lena-nb-5G-scenario-optimized"

commands = [
    "simple",
    "checkpointing",
    # "deadreckoning",
]


class SimulationParameters:
    def __init__(
        self,
        simulation,
        sim_name,
        random_seed,
        payload_size=1024,
        sync_frequency=40.0,
        packet_chance=0.3,
        edt=True,
        mobility_file="./50_ues.tcl",
        lib_path="./build/lib",
    ):
        self.simulation = simulation  # .cc file to execute
        self.sim_name = sim_name
        self.random_seed = random_seed  # For Random Number Generator
        self.payload_size = payload_size  # in Bytes
        self.packet_chance = packet_chance  # in Bytes
        self.sync_frequency = sync_frequency  # in Seconds
        self.edt = edt  # If Early Data Transmission should be used
        self.mobility_file = mobility_file
        self.lib_path = (
            lib_path  # Use this path instead of whatever LD_LIBRARY_PATH holds
        )

    def getLibPath(self):
        return self.lib_path

    def generateExecutableCall(self):
        call = self.simulation
        call += f" --simName={self.sim_name}"
        call += f" --randomSeed={self.random_seed}"
        call += f" --packetChance={self.packet_chance}"
        call += f" --payloadSize={self.payload_size}"
        call += f" --syncFrequency={self.sync_frequency}"
        call += f" --edt={self.edt}"
        call += f" --mobilityFile={self.mobility_file}"
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
            # print(simulationParameters)
            cmd = simulationParameters[0].generateExecutableCall()
            cmd += f" --worker={id}"
            print(simulationParameters[0].random_seed, id)
            # print(cmd)
            try:
                with open(
                    f"logs/{simulationParameters[0].sim_name}_{simulationParameters[0].random_seed}_{id}.log",
                    "a",
                ) as f:
                    result = subprocess.run(
                        cmd,
                        shell=True,
                        check=True,
                        text=True,
                        stdout=f,
                        stderr=subprocess.STDOUT,
                        env={"LD_LIBRARY_PATH": simulationParameters[0].getLibPath()},
                        # env=os.environ,
                    )
                    if result.returncode != 0:
                        # Task failed, maybe because of missing resources
                        # put simulation back on stac
                        print(
                            f"Something failed! Return code: {result.returncode}. Output: {result.stdout}. Error: {result.stderr}. Original cmd: {cmd}"
                        )
                        self.put(simulationParameters)
            except Exception as e:
                # Task failed, maybe because of missing resources
                # put simulation back on stac
                print(e)
                # print(
                #     f"Something failed due to unknown exception... Original cmd: {cmd}"
                # )
                self.put(simulationParameters)

            self.task_done()


start_time = time.time()
simu_queue = TaskQueue(
    7
)  # This is the number of parallel workers. This number should be below your number of CPU cores. Note that more parallel workers consume more RAM
seed = (
    33  # Number of runs. 5 means that simulations with seeds 1,2,3,4,5 will be started
)
for i in range(1, seed + 1):
    for command in commands:
        # Default
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_default",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
            )
        )

        # Cobertura
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_coverage",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
                packet_chance=0.5,
            )
        )

        # Quantidade de Nós
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_node_amount",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
                mobility_file="./100_ues.tcl",
            )
        )

        # Tamanho do Payload
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_payload_size",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
                payload_size=4096,
            )
        )

        # Modo de Transmissão
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_transmission_mode",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
                edt=False,
            )
        )

        # Frequência de Coleta de Dados de Rastreamento
        simu_queue.add_task(
            SimulationParameters(
                sim_name=f"{command}_sync_frequency",
                simulation=f"./build/scratch/{command}",
                random_seed=i,
                sync_frequency=60.0,
            )
        )
simu_queue.start_workers()

simu_queue.join()
print("--- %s seconds ---" % (time.time() - start_time))
