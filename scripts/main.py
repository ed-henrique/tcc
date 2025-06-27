import os
import glob
import pandas as pd


STARTING_ENERGY = 18000.0


def main():
    dfs = []
    for f in glob.glob("../logs/simple_default/**/*_Energy.log"):
        df = pd.read_csv(
            f,
            names=["id", "energy_remaining"],
            usecols=[0, 2],
        )
        df["energy_consumed"] = STARTING_ENERGY - df["energy_remaining"]
        dfs.append(df)
        # print(df)

    df = pd.concat(dfs)
    df = df.groupby("id").agg(pd.Series.mean)
    # print(df)

    dfs = []
    for f in glob.glob("../logs/simple_default/**/*_DataTrans.log"):
        df = pd.read_csv(f, names=["id", "sent_data_timestamp"])
        df["sent_data_amount"] = df.groupby("id").transform("size")
        # print(df)

        dfs.append(df)

    df = pd.concat(dfs)
    print(df)


if __name__ == "__main__":
    main()
