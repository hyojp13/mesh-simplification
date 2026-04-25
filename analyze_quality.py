#!/usr/bin/env python3
"""Compare two OBJ meshes using structural stats and lightweight render diffs."""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict, deque
from dataclasses import dataclass
from pathlib import Path

from render_latex_outputs import parse_obj, render_obj_to_grayscale


PROJECTIONS = ("xy", "xz", "yz")


@dataclass(frozen=True)
class MeshStats:
  vertex_count: int
  face_count: int
  degenerate_faces: int
  connected_components: int


def structural_stats(vertices: list[tuple[float, float, float]], faces: list[tuple[int, int, int]]) -> MeshStats:
  degenerate_faces = 0
  adjacency: dict[int, set[int]] = defaultdict(set)
  active_vertices: set[int] = set()

  for i0, i1, i2 in faces:
    if i0 == i1 or i1 == i2 or i0 == i2:
      degenerate_faces += 1
      continue

    active_vertices.update((i0, i1, i2))
    adjacency[i0].update((i1, i2))
    adjacency[i1].update((i0, i2))
    adjacency[i2].update((i0, i1))

  visited: set[int] = set()
  connected_components = 0
  for start in active_vertices:
    if start in visited:
      continue
    connected_components += 1
    frontier = deque([start])
    visited.add(start)
    while frontier:
      vertex = frontier.popleft()
      for neighbor in adjacency[vertex]:
        if neighbor in visited:
          continue
        visited.add(neighbor)
        frontier.append(neighbor)

  if not active_vertices and vertices:
    connected_components = len(vertices)

  return MeshStats(
      vertex_count=len(vertices),
      face_count=len(faces),
      degenerate_faces=degenerate_faces,
      connected_components=connected_components,
  )


def render_disagreement(
    reference_vertices: list[tuple[float, float, float]],
    reference_faces: list[tuple[int, int, int]],
    candidate_vertices: list[tuple[float, float, float]],
    candidate_faces: list[tuple[int, int, int]],
    projection: str,
    width: int,
    height: int,
    padding: int,
    pixel_threshold: int,
) -> float:
  reference = render_obj_to_grayscale(reference_vertices, reference_faces, projection, width, height, padding)
  candidate = render_obj_to_grayscale(candidate_vertices, candidate_faces, projection, width, height, padding)
  differing_pixels = sum(
      1
      for reference_pixel, candidate_pixel in zip(reference, candidate, strict=True)
      if abs(reference_pixel - candidate_pixel) > pixel_threshold
  )
  return differing_pixels / len(reference) if reference else 0.0


def write_csv(output_path: Path, rows: list[tuple[str, str, float | int | str]]) -> None:
  output_path.parent.mkdir(parents=True, exist_ok=True)
  with output_path.open("w", encoding="utf-8", newline="") as handle:
    writer = csv.writer(handle)
    writer.writerow(("scope", "name", "value"))
    writer.writerows(rows)


def main() -> int:
  parser = argparse.ArgumentParser(description="Compare two OBJ meshes using lightweight quality metrics.")
  parser.add_argument("reference", type=Path, help="Reference OBJ, typically the sequential output")
  parser.add_argument("candidate", type=Path, help="Candidate OBJ, typically the partitioned_batch output")
  parser.add_argument("--width", type=int, default=512, help="Render width")
  parser.add_argument("--height", type=int, default=512, help="Render height")
  parser.add_argument("--padding", type=int, default=24, help="Render padding")
  parser.add_argument("--pixel-threshold", type=int, default=20, help="Minimum grayscale delta to count as a disagreement")
  parser.add_argument("--max-render-diff", type=float, default=0.08, help="Flag runs above this average render disagreement")
  parser.add_argument("--max-component-increase", type=int, default=1, help="Flag runs above this connected-component increase")
  parser.add_argument("--output", type=Path, help="Optional CSV output path")
  args = parser.parse_args()

  reference_vertices, reference_faces = parse_obj(args.reference)
  candidate_vertices, candidate_faces = parse_obj(args.candidate)

  reference_stats = structural_stats(reference_vertices, reference_faces)
  candidate_stats = structural_stats(candidate_vertices, candidate_faces)

  projection_diffs: dict[str, float] = {}
  for projection in PROJECTIONS:
    projection_diffs[projection] = render_disagreement(
        reference_vertices,
        reference_faces,
        candidate_vertices,
        candidate_faces,
        projection,
        args.width,
        args.height,
        args.padding,
        args.pixel_threshold,
    )

  average_render_diff = sum(projection_diffs.values()) / len(projection_diffs)
  component_increase = candidate_stats.connected_components - reference_stats.connected_components
  render_flag = average_render_diff > args.max_render_diff
  component_flag = component_increase > args.max_component_increase

  print(f"reference: {args.reference}")
  print(f"candidate: {args.candidate}")
  print(
      "reference_stats:"
      f" vertices={reference_stats.vertex_count}"
      f" faces={reference_stats.face_count}"
      f" degenerate_faces={reference_stats.degenerate_faces}"
      f" connected_components={reference_stats.connected_components}"
  )
  print(
      "candidate_stats:"
      f" vertices={candidate_stats.vertex_count}"
      f" faces={candidate_stats.face_count}"
      f" degenerate_faces={candidate_stats.degenerate_faces}"
      f" connected_components={candidate_stats.connected_components}"
  )
  for projection, disagreement in projection_diffs.items():
    print(f"render_diff[{projection}]={disagreement:.6f}")
  print(f"render_diff_avg={average_render_diff:.6f}")
  print(f"connected_component_increase={component_increase}")
  print(f"quality_flagged={'yes' if render_flag or component_flag else 'no'}")

  if args.output is not None:
    rows: list[tuple[str, str, float | int | str]] = [
        ("reference", "vertex_count", reference_stats.vertex_count),
        ("reference", "face_count", reference_stats.face_count),
        ("reference", "degenerate_faces", reference_stats.degenerate_faces),
        ("reference", "connected_components", reference_stats.connected_components),
        ("candidate", "vertex_count", candidate_stats.vertex_count),
        ("candidate", "face_count", candidate_stats.face_count),
        ("candidate", "degenerate_faces", candidate_stats.degenerate_faces),
        ("candidate", "connected_components", candidate_stats.connected_components),
        ("summary", "render_diff_avg", average_render_diff),
        ("summary", "connected_component_increase", component_increase),
        ("summary", "quality_flagged", "yes" if render_flag or component_flag else "no"),
    ]
    rows.extend(("render_diff", projection, disagreement) for projection, disagreement in projection_diffs.items())
    write_csv(args.output, rows)

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
