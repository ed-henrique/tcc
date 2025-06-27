import glob
import pandas as pd
import matplotlib.pyplot as plt

LOGS_DIR = "logs"
STARTING_ENERGY = 18000.0
POSITION_GATHERING_ENERGY = 0.033


def detectar_consumo_de_energia_por_no(cmd: str) -> pd.DataFrame:
    dfs_lena = {}
    for f in glob.glob(LOGS_DIR + f"/{cmd}/**/*_Energy.log"):
        filename = f.split("/")[-1]
        seed = int(filename.split("_")[-2])

        df = pd.read_csv(
            f,
            names=["id", "energy_remaining"],
            usecols=[0, 2],
        )
        df["energy_consumed"] = STARTING_ENERGY - df["energy_remaining"]
        df = df.drop("energy_remaining", axis=1)

        dfs_lena[seed] = df

    dfs_log = {}
    for f in glob.glob(LOGS_DIR + f"/{cmd}_*.log"):
        filename = f.split("/")[-1]
        seed = int(filename.split("_")[-2])

        nodes = []

        with open(f, "r") as content:
            for line in content:
                if "consumed" in line:
                    dados = line.split()

                    nodes.append(int(dados[1]))

        df = pd.DataFrame(nodes, columns=["id"])
        df["id"] = df["id"] + 1
        df["energy_consumed"] = POSITION_GATHERING_ENERGY
        df = df.groupby("id").agg(pd.Series.sum)

        dfs_log[seed] = df

    dfs = []
    for k, v in dfs_lena.items():
        df = pd.concat([v, dfs_log[k]])
        df = df.groupby("id").agg(pd.Series.sum)

        dfs.append(df)

    df = pd.concat(dfs)
    df = df.groupby("id").agg(pd.Series.mean)

    return df


def detectar_colisoes(cmd: str) -> pd.DataFrame:
    dfs = []
    for f in glob.glob(LOGS_DIR + f"/{cmd}/**/*_Spectral_Uplink.log"):
        df = pd.read_csv(f, names=[f"cell_{i}" for i in range(13)])
        df = df.drop("cell_12", axis=1)
        df = df[~df.isin([0, -1]).sum(axis=1) >= 2]

        dfs.append(df)

    return pd.concat(dfs)


def main():
    cmds = [
        "simple",
        "checkpointing",
    ]

    variants = {
        "default": "Padrão",
        "coverage": "Alcance da Torre",
        "node_amount": "Densidade da Rede",
        "payload_size": "Tamanho do Payload",
        "sync_frequency": "Frequência de Envio dos Dados",
        "transmission_mode": "Modo de Transmissão",
    }

    dfs_consumo = {}
    for variant in variants:
        dfs_consumo[variant] = {}

    for cmd in cmds:
        for variant in variants:
            # Detecção do consumo de energia por nó
            #
            # A energia gasta por nó é obtida pelo modelo de energia do LENA-NB e uma
            # estimativa a partir dos logs de coleta de posição.
            df_consumo = detectar_consumo_de_energia_por_no(f"{cmd}_{variant}")
            dfs_consumo[variant][cmd] = df_consumo

            # Detecção de colisões
            #
            # As colisões ocorrem quando ao menos 2 colunas de uma linha qualquer têm o
            # mesmo valor, que é diferente de -1. O índice dessa linha nos dá o momento
            # em milissegundos em que ocorreu a colisão, e o valor repetido é o ID
            # daquela UE numa célula específica do eNB.
            #
            # OBS: Se não houverem colisões, a simulação é duvidosa e/ou inútil, por
            # não estar testando os limites daquele cenário.
            # df_colisoes = detectar_colisoes(f"{cmd}_{variant}")
            # print(f"{cmd}_{variant} (Colisões)")
            # print(df_colisoes)

    n_envs = len(dfs_consumo)
    fig, axes = plt.subplots(n_envs, 1, figsize=(12, 4 * n_envs), sharex=True)

    # If only one environment, convert axes to array for consistent handling
    if n_envs == 1:
        axes = [axes]

    # Plot each environment
    for (env_name, algorithms), ax in zip(dfs_consumo.items(), axes):
        # Plot both algorithms
        algorithms["simple"]["energy_consumed"].plot(
            ax=ax, label="Simple", color="blue", marker="o", linestyle="-"
        )
        algorithms["checkpointing"]["energy_consumed"].plot(
            ax=ax, label="Checkpoint", color="red", marker="x", linestyle="--"
        )

        ax.set_title(f"Cenário: {variants[env_name]}")
        ax.set_ylabel("Energia Consumida")
        ax.grid(True)
        ax.legend()

    plt.xlabel("ID")
    plt.tight_layout()
    plt.savefig("energy_consumed_per_environment.png")
    plt.close()

    # Comparação entre cenário Padrão e demais cenários
    non_default_envs = [env for env in dfs_consumo.keys() if env != "default"]

    n_envs = len(non_default_envs)
    fig, axes = plt.subplots(
        n_envs, len(cmds), figsize=(16, 4 * n_envs), sharex="col", sharey="row"
    )

    if n_envs == 1:
        axes = axes.reshape(1, -1)

    for row, env in enumerate(non_default_envs):
        for i, cmd in enumerate(cmds):
            dfs_consumo["default"][cmd]["energy_consumed"].plot(
                ax=axes[row, i], label="Padrão", color="blue", marker="o", linestyle="-"
            )
            dfs_consumo[env][cmd]["energy_consumed"].plot(
                ax=axes[row, i], label=env, color="green", marker="x", linestyle="--"
            )
            axes[row, i].set_title(
                f"Algoritmo {cmd.title()}: Padrão vs {variants[env]}"
            )
            axes[row, i].set_ylabel("Energia Consumida")
            axes[row, i].grid(True)
            axes[row, i].legend()

    plt.xlabel("ID")
    plt.tight_layout()
    plt.savefig("energy_consumed_per_cmd.png")
    plt.close()


if __name__ == "__main__":
    main()
