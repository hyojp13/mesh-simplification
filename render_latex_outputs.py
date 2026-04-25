#!/usr/bin/env python3
"""Render side-view previews of OBJ outputs and generate a LaTeX figure."""

from __future__ import annotations

import argparse
import re
import struct
import zlib
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path


OUTPUT_RE = re.compile(r"^(?P<mesh>[A-Za-z0-9_-]+)_r(?P<label>[0-9]+(?:p[0-9]+)?)_t(?P<threads>\d+)\.obj$")
TARGETS = ("100", "25", "10", "1", "0.5")
MESHES = ("bunny", "buddha", "dragon")
DEFAULT_PROJECTIONS = {
    "bunny": "xy",
    "buddha": "-xy",
    "dragon": "yz",
}
DEFAULT_ROTATIONS = {
    "dragon": "ccw",
}


@dataclass(frozen=True)
class RenderJob:
  mesh: str
  target: str
  threads: int
  obj_path: Path
  image_path: Path


def label_to_target(label: str) -> str:
  return label.replace("p", ".")


def target_to_label(target: str) -> str:
  return target.replace(".", "p")


def latex_escape(text: str) -> str:
  replacements = {
      "\\": r"\textbackslash{}",
      "&": r"\&",
      "%": r"\%",
      "$": r"\$",
      "#": r"\#",
      "_": r"\_",
      "{": r"\{",
      "}": r"\}",
      "~": r"\textasciitilde{}",
      "^": r"\textasciicircum{}",
  }
  return "".join(replacements.get(char, char) for char in text)


def discover_outputs(output_dir: Path, targets: tuple[str, ...]) -> dict[tuple[str, str, int], Path]:
  outputs: dict[tuple[str, str, int], Path] = {}
  for obj_path in output_dir.glob("*.obj"):
    match = OUTPUT_RE.match(obj_path.name)
    if not match:
      continue
    mesh = match.group("mesh")
    target = label_to_target(match.group("label"))
    threads = int(match.group("threads"))
    if target in targets:
      outputs[(mesh, target, threads)] = obj_path
  return outputs


def add_originals(outputs: dict[tuple[str, str, int], Path], root: Path, meshes: tuple[str, ...]) -> None:
  present_threads_by_mesh: dict[str, set[int]] = {}
  for mesh, _target, threads in outputs:
    present_threads_by_mesh.setdefault(mesh, set()).add(threads)

  for mesh in meshes:
    original_path = root / f"{mesh}.obj"
    if not original_path.exists():
      continue
    threads = present_threads_by_mesh.get(mesh) or {1}
    for thread_count in threads:
      outputs.setdefault((mesh, "100", thread_count), original_path)


def parse_obj(path: Path) -> tuple[list[tuple[float, float, float]], list[tuple[int, int, int]]]:
  vertices: list[tuple[float, float, float]] = []
  faces: list[tuple[int, int, int]] = []

  with path.open("r", encoding="utf-8", errors="ignore") as obj_file:
    for line in obj_file:
      if line.startswith("v "):
        parts = line.split()
        if len(parts) >= 4:
          vertices.append((float(parts[1]), float(parts[2]), float(parts[3])))
      elif line.startswith("f "):
        parts = line.split()[1:]
        if len(parts) != 3:
          continue
        indices: list[int] = []
        for token in parts:
          index = int(token.split("/", 1)[0])
          if index > 0:
            indices.append(index - 1)
          else:
            indices.append(len(vertices) + index)
        if all(0 <= index < len(vertices) for index in indices):
          faces.append((indices[0], indices[1], indices[2]))

  return vertices, faces


def projection_for_mesh(mesh: str, projection: str) -> str:
  if projection != "auto":
    return projection
  return DEFAULT_PROJECTIONS.get(mesh, "xz")


def rotation_for_mesh(mesh: str, projection: str) -> str:
  if projection != "auto":
    return "none"
  return DEFAULT_ROTATIONS.get(mesh, "none")


def project_vertex(vertex: tuple[float, float, float], projection: str) -> tuple[float, float, float]:
  x, y, z = vertex
  reverse_depth = projection.startswith("-")
  plane = projection[1:] if reverse_depth else projection

  if plane == "xy":
    projected = (x, y, z)
  elif plane == "yz":
    projected = (y, z, x)
  else:
    projected = (x, z, y)

  u, v, depth = projected
  return u, v, -depth if reverse_depth else depth


def normalized_points(vertices: list[tuple[float, float, float]],
                      projection: str,
                      width: int,
                      height: int,
                      padding: int) -> list[tuple[float, float, float]]:
  projected = [project_vertex(vertex, projection) for vertex in vertices]
  min_x = min(point[0] for point in projected)
  max_x = max(point[0] for point in projected)
  min_y = min(point[1] for point in projected)
  max_y = max(point[1] for point in projected)

  span_x = max(max_x - min_x, 1.0e-12)
  span_y = max(max_y - min_y, 1.0e-12)
  drawable_width = max(width - 2 * padding, 1)
  drawable_height = max(height - 2 * padding, 1)
  scale = min(drawable_width / span_x, drawable_height / span_y)
  offset_x = (width - span_x * scale) / 2.0
  offset_y = (height - span_y * scale) / 2.0

  points: list[tuple[float, float, float]] = []
  for x, y, depth in projected:
    px = offset_x + (x - min_x) * scale
    py = height - (offset_y + (y - min_y) * scale)
    points.append((px, py, depth))
  return points


def edge_function(a: tuple[float, float, float], b: tuple[float, float, float], x: float, y: float) -> float:
  return (x - a[0]) * (b[1] - a[1]) - (y - a[1]) * (b[0] - a[0])


def face_shade(vertices: list[tuple[float, float, float]], face: tuple[int, int, int], projection: str) -> int:
  v0 = vertices[face[0]]
  v1 = vertices[face[1]]
  v2 = vertices[face[2]]
  edge01 = (v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2])
  edge02 = (v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2])
  normal = (
      edge01[1] * edge02[2] - edge01[2] * edge02[1],
      edge01[2] * edge02[0] - edge01[0] * edge02[2],
      edge01[0] * edge02[1] - edge01[1] * edge02[0],
  )
  length = max((normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]) ** 0.5, 1.0e-12)
  normal = (normal[0] / length, normal[1] / length, normal[2] / length)

  reverse_depth = projection.startswith("-")
  plane = projection[1:] if reverse_depth else projection
  if plane == "xy":
    view = (0.0, 0.0, -1.0 if reverse_depth else 1.0)
  elif plane == "yz":
    view = (-1.0 if reverse_depth else 1.0, 0.0, 0.0)
  else:
    view = (0.0, -1.0 if reverse_depth else 1.0, 0.0)

  facing = abs(normal[0] * view[0] + normal[1] * view[1] + normal[2] * view[2])
  return int(round(215 - 85 * facing))


def rasterize_triangle(buffer: bytearray,
                       depth_buffer: list[float],
                       points: list[tuple[float, float, float]],
                       face: tuple[int, int, int],
                       color: int,
                       width: int,
                       height: int) -> None:
  p0 = points[face[0]]
  p1 = points[face[1]]
  p2 = points[face[2]]
  area = edge_function(p0, p1, p2[0], p2[1])
  if abs(area) < 1.0e-8:
    return

  min_x = max(0, math_floor(min(p0[0], p1[0], p2[0])))
  max_x = min(width - 1, math_ceil(max(p0[0], p1[0], p2[0])))
  min_y = max(0, math_floor(min(p0[1], p1[1], p2[1])))
  max_y = min(height - 1, math_ceil(max(p0[1], p1[1], p2[1])))

  for y in range(min_y, max_y + 1):
    py = y + 0.5
    for x in range(min_x, max_x + 1):
      px = x + 0.5
      w0 = edge_function(p1, p2, px, py) / area
      w1 = edge_function(p2, p0, px, py) / area
      w2 = edge_function(p0, p1, px, py) / area
      if w0 < 0.0 or w1 < 0.0 or w2 < 0.0:
        continue

      depth = w0 * p0[2] + w1 * p1[2] + w2 * p2[2]
      index = y * width + x
      if depth > depth_buffer[index]:
        depth_buffer[index] = depth
        buffer[index] = color


def math_floor(value: float) -> int:
  return int(value // 1)


def math_ceil(value: float) -> int:
  return int(-(-value // 1))


def render_obj_to_grayscale(vertices: list[tuple[float, float, float]],
                            faces: list[tuple[int, int, int]],
                            projection: str,
                            width: int,
                            height: int,
                            padding: int) -> bytes:
  buffer = bytearray([255] * (width * height))
  if not vertices:
    return bytes(buffer)

  points = normalized_points(vertices, projection, width, height, padding)
  depth_buffer = [-float("inf")] * (width * height)

  if faces:
    for face in faces:
      rasterize_triangle(buffer, depth_buffer, points, face, face_shade(vertices, face, projection), width, height)
  else:
    for point in points:
      x, y, depth = point
      ix = int(round(x))
      iy = int(round(y))
      if 0 <= ix < width and 0 <= iy < height:
        index = iy * width + ix
        if depth > depth_buffer[index]:
          depth_buffer[index] = depth
          buffer[index] = 45

  return bytes(buffer)


def pdf_object(object_number: int, payload: bytes) -> bytes:
  return f"{object_number} 0 obj\n".encode() + payload + b"\nendobj\n"


def write_grayscale_pdf(path: Path, pixels: bytes, width: int, height: int) -> None:
  compressed = zlib.compress(pixels)
  objects: list[bytes] = [
      pdf_object(1, b"<< /Type /Catalog /Pages 2 0 R >>"),
      pdf_object(2, b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"),
      pdf_object(
          3,
          (f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] "
           f"/Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>").encode(),
      ),
      pdf_object(
          4,
          (f"<< /Type /XObject /Subtype /Image /Width {width} /Height {height} "
           f"/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /Length {len(compressed)} >>\n"
           "stream\n").encode() + compressed + b"\nendstream",
      ),
  ]
  content = f"q\n{width} 0 0 {height} 0 0 cm\n/Im0 Do\nQ\n".encode()
  objects.append(pdf_object(5, f"<< /Length {len(content)} >>\nstream\n".encode() + content + b"endstream"))

  output = bytearray(b"%PDF-1.4\n")
  offsets = [0]
  for obj in objects:
    offsets.append(len(output))
    output.extend(obj)

  xref_offset = len(output)
  output.extend(f"xref\n0 {len(objects) + 1}\n".encode())
  output.extend(b"0000000000 65535 f \n")
  for offset in offsets[1:]:
    output.extend(f"{offset:010d} 00000 n \n".encode())
  output.extend(
      (f"trailer\n<< /Size {len(objects) + 1} /Root 1 0 R >>\n"
       f"startxref\n{xref_offset}\n%%EOF\n").encode()
  )
  path.write_bytes(output)


def rotate_grayscale_ccw(pixels: bytes, width: int, height: int) -> tuple[bytes, int, int]:
  rotated_width = height
  rotated_height = width
  rotated = bytearray(len(pixels))
  for y in range(height):
    for x in range(width):
      rotated_x = y
      rotated_y = width - 1 - x
      rotated[rotated_y * rotated_width + rotated_x] = pixels[y * width + x]
  return bytes(rotated), rotated_width, rotated_height


def render_job(job: RenderJob, projection: str, width: int, height: int, padding: int) -> None:
  vertices, faces = parse_obj(job.obj_path)
  render_width = width
  render_height = height
  pixels = render_obj_to_grayscale(vertices, faces, projection_for_mesh(job.mesh, projection), width, height, padding)
  if rotation_for_mesh(job.mesh, projection) == "ccw":
    pixels, render_width, render_height = rotate_grayscale_ccw(pixels, width, height)
  job.image_path.parent.mkdir(parents=True, exist_ok=True)
  write_grayscale_pdf(job.image_path, pixels, render_width, render_height)


def ordered_threads(outputs: dict[tuple[str, str, int], Path]) -> list[int]:
  return sorted({threads for _mesh, _target, threads in outputs})


def build_jobs(root: Path,
               output_dir: Path,
               image_dir: Path,
               meshes: tuple[str, ...],
               targets: tuple[str, ...],
               outputs: dict[tuple[str, str, int], Path]) -> list[RenderJob]:
  jobs: list[RenderJob] = []
  threads = ordered_threads(outputs) or [1]
  for mesh in meshes:
    for thread_count in threads:
      for target in targets:
        obj_path = outputs.get((mesh, target, thread_count))
        if obj_path is None:
          continue
        image_name = f"{mesh}_r{target_to_label(target)}_t{thread_count}.pdf"
        jobs.append(RenderJob(mesh, target, thread_count, obj_path, image_dir / image_name))
  return jobs


def generate_latex(jobs: list[RenderJob],
                   output_dir: Path,
                   meshes: tuple[str, ...],
                   targets: tuple[str, ...],
                   columns_per_row: int,
                   caption: str) -> str:
  jobs_by_key = {(job.mesh, job.target, job.threads): job for job in jobs}
  threads = sorted({job.threads for job in jobs}) or [1]
  column_count = max(1, min(columns_per_row, len(targets)))
  image_width = 0.98 / column_count

  lines = [
      r"% Generated by render_latex_outputs.py.",
      r"% Preamble requirement: \usepackage{graphicx}",
      "",
      r"\begin{figure}[htbp]",
      r"\centering",
  ]

  for mesh in meshes:
    mesh_jobs = [job for job in jobs if job.mesh == mesh]
    if not mesh_jobs:
      continue

    lines.append(f"\\textbf{{{latex_escape(mesh)}}}\\\\[0.4em]")
    for thread_count in threads:
      thread_jobs = [job for job in mesh_jobs if job.threads == thread_count]
      if not thread_jobs:
        continue
      if len(threads) > 1:
        lines.append(f"\\textit{{threads={thread_count}}}\\\\[0.2em]")
      lines.append(r"\noindent\makebox[\textwidth][c]{%")
      for index, target in enumerate(targets, start=1):
        job = jobs_by_key.get((mesh, target, thread_count))
        if job is None:
          continue
        rel_path = job.image_path.relative_to(output_dir.parent)
        lines.extend([
            rf"\begin{{minipage}}{{{image_width:.4f}\paperwidth}}",
            r"\centering",
            rf"\includegraphics[width={image_width:.4f}\paperwidth]{{\detokenize{{{str(rel_path)}}}}}\\[-0.2em]",
            rf"\scriptsize {latex_escape(target)}\%",
            r"\end{minipage}%",
        ])
        if index % column_count == 0:
          lines.append(r"}\\[0.8em]")
      if sum(1 for target in targets if jobs_by_key.get((mesh, target, thread_count)) is not None) % column_count != 0:
        lines.append(r"}\\[0.8em]")
      lines.append(r"\\[0.4em]")
    lines.append(r"\vspace{0.8em}")

  lines.extend([
      rf"\caption{{{latex_escape(caption)}}}",
      r"\label{fig:mesh-side-renders}",
      r"\end{figure}",
      "",
  ])
  return "\n".join(lines)


def main() -> int:
  parser = argparse.ArgumentParser(description="Render OBJ outputs from the side and generate a LaTeX figure.")
  parser.add_argument(
      "--output-dir",
      type=Path,
      default=Path("output/sequential"),
      help="Directory containing sweep OBJ outputs",
  )
  parser.add_argument("--image-dir", type=Path, help="Directory for rendered PDFs; defaults to <output-dir>/renders")
  parser.add_argument("--tex", type=Path, help="Output LaTeX file; defaults to <output-dir>/rendered_meshes.tex")
  parser.add_argument(
      "--projection",
      choices=("auto", "xy", "-xy", "xz", "-xz", "yz", "-yz"),
      default="auto",
      help="Projection plane. Default auto uses bunny=xy, buddha=-xy, dragon=yz.",
  )
  parser.add_argument("--width", type=int, default=900, help="Rendered image width in pixels")
  parser.add_argument("--height", type=int, default=700, help="Rendered image height in pixels")
  parser.add_argument("--padding", type=int, default=35, help="Image padding in pixels")
  parser.add_argument("--targets", nargs="+", default=list(TARGETS), help="Target percentages to include")
  parser.add_argument("--meshes", nargs="+", default=list(MESHES), help="Meshes to include")
  parser.add_argument("--columns", type=int, default=5, help="Number of target images per row")
  args = parser.parse_args()

  root = Path.cwd()
  image_dir = args.image_dir or (args.output_dir / "renders")
  tex_path = args.tex or (args.output_dir / "rendered_meshes.tex")
  outputs = discover_outputs(args.output_dir, tuple(args.targets))
  add_originals(outputs, root, tuple(args.meshes))
  jobs = build_jobs(root, args.output_dir, image_dir, tuple(args.meshes), tuple(args.targets), outputs)

  if not jobs:
    raise SystemExit(f"No OBJ files found in {args.output_dir}")

  for job in jobs:
    render_job(job, args.projection, args.width, args.height, args.padding)

  tex = generate_latex(
      jobs,
      args.output_dir,
      tuple(args.meshes),
      tuple(args.targets),
      args.columns,
      f"Side-view renderings of simplified meshes for target vertex percentages {', '.join(args.targets)}.",
  )
  tex_path.parent.mkdir(parents=True, exist_ok=True)
  tex_path.write_text(tex, encoding="utf-8")

  print(f"Rendered {len(jobs)} images into {image_dir}")
  print(f"Wrote {tex_path}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
