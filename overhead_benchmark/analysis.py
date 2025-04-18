import numpy as np
import pandas as pd
from scipy import stats
from benchmark import SYSBENCH_ARGS, ENGINES

BENCHS: set[str] = set(SYSBENCH_ARGS)

def conf_interval(data, conf: float = 0.95):
    # Confidence level
    confidence = 0.95

    # Sample mean and standard error
    mean = np.mean(data)
    sem = stats.sem(data)  # Standard error of the mean

    # Confidence interval
    ci = stats.t.interval(conf, df=len(data)-1, loc=mean, scale=sem)
    
    print(f"Mean: {mean:.3f}")
    print(f"{confidence*100:.0f}% Confidence Interval: {ci}")

    return ci

def main():
    df = pd.read_csv("data.csv", names=["engine", "bench", "res"])
    for bench in BENCHS:
        df_bench = df[df["bench"] == bench]
        for engine in ENGINES:
            data = df[df["engine"] == engine].values
            print("dat")
            print(data)
            conf_interval(data)



if __name__ == "__main__":
    main()


