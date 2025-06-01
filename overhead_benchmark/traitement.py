# -*- coding: utf-8 -*-
"""Container Runtime Performance Analysis - Clean Architecture"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from typing import List, Dict, Tuple

# Constants
RUNTIMES = ["runc", "crun", "docker", "podman", "youki", "light-cont", "native"]
OVERHEADS = ["fileio", "cpu", "memory", "launchtime"]
METRICS = {
    "fileio": "MiB/s",
    "cpu": "Events/sec",
    "memory": "MiB/sec",
    "launchtime": "seconds"
}
PLOT_SIZE = (18, 20)
DPI = 300

def load_and_clean_data(filepath: str) -> pd.DataFrame:
    """Load and validate the performance data from CSV"""
    try:
        # Read raw data with maximum flexibility
        with open(filepath, 'r') as f:
            lines = [line.strip() for line in f if line.strip()]

        # Process each line to extract the three components
        processed = []
        for line in lines:
            parts = [p.strip() for p in line.split(',')]
            if len(parts) >= 3:  # Take last 3 components if more exist
                processed.append(parts[-3:])

        # Create DataFrame and convert values
        df = pd.DataFrame(processed, columns=['runtime', 'overhead', 'value'])
        df['value'] = pd.to_numeric(df['value'].str.replace('[^\d.]', '', regex=True))

        return df.dropna()

    except Exception as e:
        print(f"Data loading failed: {str(e)}")
        print("\nFirst 3 problematic lines:")
        for line in lines[:3]:
            print(line)
        exit()

def calculate_statistics(data: pd.Series) -> Tuple[float, float]:
    """Calculate mean and 95% confidence interval"""
    mean = data.mean()
    ci = 1.96 * data.sem()  # 95% CI
    return mean, ci

def create_visualization(data: pd.DataFrame, output_path: str):
    """Generate the complete visualization grid"""
    fig, axes = plt.subplots(
        len(RUNTIMES),
        len(OVERHEADS),
        figsize=PLOT_SIZE,
        sharex='col'
    )

    for i, runtime in enumerate(RUNTIMES):
        for j, overhead in enumerate(OVERHEADS):
            ax = axes[i, j]
            plot_runtime_overhead(data, runtime, overhead, ax)

    plt.tight_layout(pad=2.5)
    plt.savefig(output_path, dpi=DPI, bbox_inches='tight')
    plt.show()

def plot_runtime_overhead(data: pd.DataFrame, runtime: str, overhead: str, ax):
    """Plot individual runtime/overhead combination"""
    subset = data[(data['runtime'] == runtime) & (data['overhead'] == overhead)]

    if subset.empty:
        ax.text(0.5, 0.5, 'No Data', ha='center', va='center')
        ax.set_xticks([])
        ax.set_yticks([])
        return

    values = subset['value']
    mean, ci = calculate_statistics(values)

    # Plot data points
    ax.scatter(
        np.arange(len(values)),
        values,
        alpha=0.7,
        color='#1f77b4',
        label=f'{len(values)} samples'
    )

    # Plot statistics
    ax.axhline(mean, color='#d62728', linestyle='--', linewidth=1.5)
    ax.fill_between(
        ax.get_xlim(),
        mean - ci,
        mean + ci,
        color='#d62728',
        alpha=0.2,
        label='95% CI'
    )

    # Configure axes
    ax.set_title(f"{runtime} - {overhead}", pad=10)
    if runtime == RUNTIMES[-1]:
        ax.set_xlabel("Test Iteration", labelpad=8)
    if overhead == OVERHEADS[0]:
        ax.set_ylabel(METRICS[overhead], labelpad=8)

    ax.grid(True, alpha=0.2)
    ax.legend(loc='upper right', fontsize=8)

def main():
    """Main execution flow"""
    print("Loading and processing data...")
    df = load_and_clean_data('data.csv')
    print("\nData sample:")
    print(df.head())

    print("\nGenerating visualization...")
    create_visualization(df, 'runtime_performance.png')
    print("Visualization saved as 'runtime_performance.png'")

if __name__ == "__main__":
    main()
