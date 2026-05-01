#!/usr/bin/env python3

from pathlib import Path
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


INPUT_DIR = Path("./qes_batch/partitioned_batch")
OUTPUT_DIR = INPUT_DIR / "renders"

# Match bunny_r25_t16.obj, bunny_r10_t16.obj, bunny_r1_t16.obj, bunny_r0p5_t16.obj
PATTERN = re.compile(r"^bunny_(r[0-9p]+)_t16\.obj$")


def load_obj(obj_path: Path):
    vertices = []
    faces = []

    with obj_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("v "):
                parts = line.strip().split()
                if len(parts) >= 4:
                    vertices.append([float(parts[1]), float(parts[2]), float(parts[3])])

            elif line.startswith("f "):
                parts = line.strip().split()[1:]
                face = []
                for token in parts:
                    # Supports:
                    # f v
                    # f v/vt
                    # f v//vn
                    # f v/vt/vn
                    idx_str = token.split("/")[0]
                    idx = int(idx_str)

                    # Handle negative indices if present
                    if idx < 0:
                        idx = len(vertices) + idx + 1

                    face.append(idx - 1)

                # Triangulate polygons if needed
                if len(face) >= 3:
                    for i in range(1, len(face) - 1):
                        faces.append([face[0], face[i], face[i + 1]])

    vertices = np.asarray(vertices, dtype=float)
    faces = np.asarray(faces, dtype=int)

    if len(vertices) == 0 or len(faces) == 0:
        raise ValueError(f"Failed to load usable geometry from {obj_path}")

    return vertices, faces


def normalize_vertices(vertices: np.ndarray):
    center = vertices.mean(axis=0)
    vertices = vertices - center

    span = vertices.max(axis=0) - vertices.min(axis=0)
    scale = np.max(span)
    if scale > 0:
        vertices = vertices / scale

    return vertices


def render_mesh_to_pdf(obj_path: Path, pdf_path: Path, max_faces: int = 80000):
    vertices, faces = load_obj(obj_path)
    vertices = normalize_vertices(vertices)

    # Downsample faces only for rendering speed if mesh is huge
    if len(faces) > max_faces:
        step = max(1, len(faces) // max_faces)
        faces = faces[::step]

    triangles = vertices[faces]

    fig = plt.figure(figsize=(4.2, 4.2))
    ax = fig.add_subplot(111, projection="3d")

    mesh = Poly3DCollection(
        triangles,
        linewidths=0.02,
        edgecolors="none",
        alpha=1.0
    )
    mesh.set_facecolor((0.72, 0.72, 0.75, 1.0))
    ax.add_collection3d(mesh)

    # Fixed side-ish view, similar to simple report rendering
    ax.view_init(elev=15, azim=-70)

    # Set equal-ish limits
    mins = vertices.min(axis=0)
    maxs = vertices.max(axis=0)
    centers = (mins + maxs) / 2.0
    radius = np.max(maxs - mins) / 2.0
    if radius == 0:
        radius = 1.0

    ax.set_xlim(centers[0] - radius, centers[0] + radius)
    ax.set_ylim(centers[1] - radius, centers[1] + radius)
    ax.set_zlim(centers[2] - radius, centers[2] + radius)

    ax.set_box_aspect((1, 1, 1))
    ax.set_axis_off()

    pdf_path.parent.mkdir(parents=True, exist_ok=True)
    plt.tight_layout(pad=0)
    fig.savefig(pdf_path, format="pdf", bbox_inches="tight", pad_inches=0.02)
    plt.close(fig)


def main():
    if not INPUT_DIR.exists():
        raise FileNotFoundError(f"Input directory not found: {INPUT_DIR}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    bunny_files = sorted(
        p for p in INPUT_DIR.glob("bunny_*.obj")
        if PATTERN.match(p.name)
    )

    if not bunny_files:
        print("No Bunny 16-thread OBJ files found.")
        print(f"Looked in: {INPUT_DIR}")
        return

    print(f"Found {len(bunny_files)} Bunny 16-thread OBJ files.")
    for obj_path in bunny_files:
        stem = obj_path.stem
        pdf_path = OUTPUT_DIR / f"{stem}.pdf"
        print(f"Rendering {obj_path} -> {pdf_path}")
        render_mesh_to_pdf(obj_path, pdf_path)

    print("\nDone.")
    print(f"Rendered PDFs are in: {OUTPUT_DIR}")


if __name__ == "__main__":
    main()