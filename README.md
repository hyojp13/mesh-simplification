# Mesh Simplification

This project provides a lightweight command-line implementation of mesh simplification based on Garland-Heckbert's "Surface Simplification Using Quadric Error Metrics".

The simplifier keeps the core half-edge topology logic from the original codebase, but removes the OpenGL viewer and all GUI-oriented dependencies. The binary now reads a triangular Wavefront `.obj` file, applies QEM-based edge collapses until the requested reduction is reached, and writes the simplified mesh back to disk.

## Features

- Dependency-free C++20 build
- Command-line interface for `input.obj -> output.obj`
- QEM simplification over a half-edge mesh
- Supports common triangular OBJ face formats such as `f v/vt/vn`, `f v//vn`, and `f v`

## Build

```bash
cmake -S . -B build
cmake --build build
```

Or with presets:

```bash
cmake --preset debug
cmake --build --preset debug
```

## Usage

```bash
./build/src/mesh_simplification input.obj output.obj 50
```

Arguments:

- `input.obj`: input triangular mesh
- `output.obj`: destination for the simplified mesh
- `50`: percent of faces to remove

The reduction argument also accepts ratios such as `0.5` or explicit percentages such as `50%`.

## Notes

- Only triangular faces are supported.
- Output OBJ files contain vertex positions and triangle faces.
