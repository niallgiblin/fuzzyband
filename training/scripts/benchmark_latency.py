#!/usr/bin/env python3
"""Benchmark PlayingStyleCNN inference latency (M009 S03).

Phase E of REAL_TIME_AUDIO_CLASSIFIER.md:
  - 1000 warmup + 1000 timed inferences on CPU
  - Report P50, P95, P99 in milliseconds
  - Target: P99 < 5ms
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import numpy as np
import torch

_MODEL_DIR = Path(__file__).resolve().parents[1] / "models"
sys.path.insert(0, str(_MODEL_DIR.parent))

from models.playing_style_cnn import PlayingStyleCNN  # noqa: E402


def benchmark(
    model: torch.nn.Module,
    n_warmup: int = 100,
    n_runs: int = 1000,
    device: torch.device = torch.device("cpu"),
) -> dict:
    """Run inference latency benchmark.

    Returns dict with p50, p95, p99, mean, min, max in milliseconds.
    """
    model.eval()
    dummy = torch.randn(1, 1, 64, 32, device=device)

    # Warmup
    with torch.no_grad():
        for _ in range(n_warmup):
            _ = model(dummy)

    # Timed runs
    times: list[float] = []
    with torch.no_grad():
        for _ in range(n_runs):
            t0 = time.perf_counter()
            _ = model(dummy)
            times.append((time.perf_counter() - t0) * 1000.0)  # ms

    arr = np.array(times)
    return {
        "n_warmup": n_warmup,
        "n_runs": n_runs,
        "device": str(device),
        "p50_ms": float(np.percentile(arr, 50)),
        "p95_ms": float(np.percentile(arr, 95)),
        "p99_ms": float(np.percentile(arr, 99)),
        "mean_ms": float(np.mean(arr)),
        "min_ms": float(np.min(arr)),
        "max_ms": float(np.max(arr)),
        "std_ms": float(np.std(arr)),
    }


def main() -> int:
    model_path = _MODEL_DIR / "best_model.pt"
    if not model_path.exists():
        print(f"ERROR: model not found at {model_path}", file=sys.stderr)
        return 1

    device = torch.device("cpu")
    model = PlayingStyleCNN(n_classes=5)
    model.load_state_dict(torch.load(model_path, map_location=device, weights_only=True))
    model.to(device)

    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model: {n_params:,} parameters")
    print(f"Device: {device}")
    print()

    results = benchmark(model, n_warmup=100, n_runs=1000, device=device)

    print("Latency Benchmark (single batch-1 inference)")
    print(f"  Warmup runs:  {results['n_warmup']}")
    print(f"  Timed runs:   {results['n_runs']}")
    print()
    print(f"  P50:  {results['p50_ms']:.3f} ms")
    print(f"  P95:  {results['p95_ms']:.3f} ms")
    print(f"  P99:  {results['p99_ms']:.3f} ms")
    print(f"  Mean: {results['mean_ms']:.3f} ms")
    print(f"  Min:  {results['min_ms']:.3f} ms")
    print(f"  Max:  {results['max_ms']:.3f} ms")
    print(f"  Std:  {results['std_ms']:.3f} ms")
    print()

    if results["p99_ms"] < 5.0:
        print("✓ P99 < 5ms — real-time feasible on background thread")
    else:
        print(f"✗ P99 {results['p99_ms']:.3f} ms exceeds 5ms target", file=sys.stderr)
        return 1

    # Throughput test: batch of n to see if batching helps
    batch_sizes = [1, 4, 16]
    print("\nBatch throughput (100 runs each):")
    for bs in batch_sizes:
        dummy_batch = torch.randn(bs, 1, 64, 32, device=device)
        times_batch: list[float] = []
        with torch.no_grad():
            for _ in range(100):
                t0 = time.perf_counter()
                _ = model(dummy_batch)
                times_batch.append((time.perf_counter() - t0) * 1000.0)
        p50 = float(np.percentile(times_batch, 50))
        print(f"  batch={bs:>2d}: P50={p50:.3f} ms  ({p50/bs:.3f} ms/sample)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
