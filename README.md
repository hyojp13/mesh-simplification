# Mesh Simplification

This project provides a lightweight command-line implementation of mesh simplification based on Garland-Heckbert's "Surface Simplification Using Quadric Error Metrics".

The simplifier keeps the core half-edge topology logic from the original codebase, but removes the OpenGL viewer and all GUI-oriented dependencies. The binary now reads a triangular Wavefront `.obj` file, applies QEM-based edge collapses until the requested reduction is reached, and writes the simplified mesh back to disk.

## Features

- Dependency-free C++20 build
- Command-line interface for `input.obj -> output.obj`
- QEM simplification over a half-edge mesh
- `sequential` control mode and `partitioned_batch` prototype mode
- Optional OpenMP acceleration for initial quadric accumulation and candidate queue construction
- Per-run CSV stats for phase timings, queue behavior, and partition metrics
- Supports common triangular OBJ face formats such as `f v/vt/vn`, `f v//vn`, and `f v`

## Build

```bash
cmake -S . -B build
cmake --build build
```

Or with presets:

```bash
cmake --preset release
cmake --build --preset release
```

## Usage

```bash
./out/build/release/src/mesh_simplification input.obj output.obj 25% \
  --mode partitioned_batch \
  --threads 8 \
  --partitions 8 \
  --batch-size 64 \
  --stats-out output/partitioned_batch/stats/bunny_r25_t8.csv
```

Arguments:

- `input.obj`: input triangular mesh
- `output.obj`: destination for the simplified mesh
- `25%`: percent of vertices to keep, written either as `25%`, `25`, or `0.25`
- `--mode`: either `sequential` or `partitioned_batch`
- `--threads`: requested OpenMP thread count for preprocessing and queue construction
- `--partitions`: spatial slab partition count for `partitioned_batch` mode
- `--batch-size`: maximum interior collapses selected per partition in each round
- `--repartition-every`: optional fixed repartition interval; defaults to an adaptive policy
- `--stats-out`: optional CSV path for phase timings and partition metrics

The legacy positional thread-count form still works:

```bash
./out/build/release/src/mesh_simplification input.obj output.obj 25% 4
```

## Sweep Script

`run_sweep.sh` now writes separate mode-specific directories under `output/`:

```bash
./run_sweep.sh
python3 generate_latex_results.py output/sequential/run_sweep.log -o output/sequential/results.tex
python3 generate_latex_results.py output/partitioned_batch/run_sweep.log -o output/partitioned_batch/results.tex
```

Useful environment overrides:

- `MODES_CSV=sequential,partitioned_batch`
- `THREAD_COUNTS_CSV=1,2,4,8,16`
- `TARGET_VERTEX_PERCENTS_CSV=25%,10%,1%,0.5%`
- `MESHES_CSV=bunny,buddha,dragon`
- `BATCH_SIZE=64`
- `REPARTITION_EVERY=0`

## Quality Checks

Compare a prototype output against the sequential control at the same target with:

```bash
python3 analyze_quality.py \
  output/sequential/bunny_r25_t1.obj \
  output/partitioned_batch/bunny_r25_t8.obj \
  --output output/partitioned_batch/quality_bunny_r25_t8.csv
```

## Notes

- Only triangular faces are supported.
- Output OBJ files contain vertex positions and triangle faces.
