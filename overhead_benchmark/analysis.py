import numpy as np
import pandas as pd
from scipy import stats
from benchmark import SYSBENCH_ARGS, ENGINES

BENCHS: set[str] = set(SYSBENCH_ARGS)

def read_csv_data(file_path="data.csv"):
    res = dict()
    file = open(file_path, "r")
    for line in file:
        engine, test, val = line.split(",")
        if (engine, test) not in res:
            res[engine, test] = [float(val)]
        else:
            res[engine, test].append(float(val))
    return res

def conf_interval(data, conf: float = 0.95):
    # Confidence level
    confidence = 0.95

    # Sample mean and standard error
    mean = np.mean(data)
    sem = stats.sem(data)  # Standard error of the mean

    # Confidence interval
    ci = stats.t.interval(conf, df=len(data)-1, loc=mean, scale=sem)
    

    return mean, ci

def main():
    confidence = 0.95
    data = read_csv_data()
    for (engine, test) in data:
        mean, ci = conf_interval(data[engine, test], confidence)
        print(engine, test)
        print(f"{confidence*100:.0f}% Confidence Interval: {ci}")
        print(f"Mean: {mean:.3f}")

if __name__ == "__main__":
    main()