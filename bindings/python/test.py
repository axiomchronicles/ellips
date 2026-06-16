"""Minimal Python binding example.

Run from the repo root after building the Python module:

    PYTHONPATH=bindings/python python3 bindings/python/test.py
    PYTHONPATH=bindings/python python3 bindings/python/test.py --gpu-algorithm brute_force
    PYTHONPATH=bindings/python python3 bindings/python/test.py --gpu-algorithm ivf_flat
    PYTHONPATH=bindings/python python3 bindings/python/test.py --gpu-algorithm ivf_pq
    PYTHONPATH=bindings/python python3 bindings/python/test.py --gpu-algorithm cagra
"""

from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

import elips


def build_gpu_config(algorithm_name: str) -> elips.GpuConfig:
    if not elips._has_gpu:
        raise RuntimeError("ELIPS was built without GPU bindings")

    algorithm = getattr(elips.GpuIndexAlgorithm, algorithm_name)

    gpu = elips.GpuConfig()
    gpu.policy = elips.GpuPolicy.prefer_gpu
    gpu.build_mode = elips.IndexBuildMode.gpu_build_gpu_serve
    gpu.algorithm = algorithm
    gpu.precision = elips.GpuPrecision.auto
    gpu.ef_search = 32
    gpu.max_batch_size = 64

    gpu.graph_params.graph_degree = 16
    gpu.graph_params.intermediate_graph_degree = 32

    gpu.ivf_pq_params.n_lists = 16
    gpu.ivf_pq_params.pq_dim = 2
    gpu.ivf_pq_params.pq_bits = 8
    gpu.ivf_pq_params.kmeans_n_iters = 12
    return gpu


def build_config(gpu_algorithm: str) -> elips.Config:
    index_name = "graph" if gpu_algorithm == "cagra" else "exact"
    config = (
        elips.Config()
        .dimension(2)
        .metric("cosine")
        .index(index_name)
        .durability("ephemeral")
    )

    if gpu_algorithm != "cpu":
        config = config.gpu(build_gpu_config(gpu_algorithm))

    return config


def main() -> None:
    parser = argparse.ArgumentParser(description="ELIPS Python binding example")
    parser.add_argument(
        "--gpu-algorithm",
        choices=("cpu", "auto", "brute_force", "ivf_flat", "ivf_pq", "cagra"),
        default="cpu",
        help="accelerator path to configure for the example database",
    )
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as td:
        db_path = Path(td) / "python-binding-example"
        db = elips.open_with_config(str(db_path), build_config(args.gpu_algorithm))
        vault = db.vault("demo")

        first = vault.place([1.0, 0.0], {"kind": "alpha"})
        second = vault.place([0.0, 1.0], {"kind": "beta"})
        third = vault.place([0.8, 0.2], {"kind": "alpha-near"})

        hits = vault.seek([1.0, 0.0], top=3)
        print("gpu_algorithm:", args.gpu_algorithm)
        print("inserted_ids:", [str(first), str(second), str(third)])
        print("top_hits:", [(hit.id, round(hit.distance, 4), hit.data) for hit in hits])

        if elips._has_gpu:
            print("gpu_info:", db.gpu_info())

        db.close()


if __name__ == "__main__":
    main()
