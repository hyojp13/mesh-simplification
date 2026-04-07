#!/usr/bin/env python3
"""Generate LaTeX plots and tables from run_sweep.log."""

from __future__ import annotations

import argparse
import re
import sys
from collections import OrderedDict
from pathlib import Path


RUN_RE = re.compile(r"^Running mesh=(?P<mesh>\S+) target_vertices=(?P<target>\S+) threads=(?P<threads>\d+)\s*$")
TIME_RE = re.compile(r"^Simplification time:\s*(?P<time>[0-9]+(?:\.[0-9]+)?)\s*s\s*$")

EXPECTED_MESHES = ("bunny", "buddha", "dragon")
EXPECTED_TARGETS = ("50%", "25%", "10%", "1%", "0.1%")
EXPECTED_THREADS = (1, 2, 4, 8, 16)


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


def ordered_insert(values: OrderedDict, key) -> None:
  if key not in values:
    values[key] = None


def parse_log(log_path: Path):
  results: dict[tuple[str, str, int], float] = {}
  mesh_order: OrderedDict[str, None] = OrderedDict()
  target_order: OrderedDict[str, None] = OrderedDict()
  thread_order: OrderedDict[int, None] = OrderedDict()
  warnings: list[str] = []
  current_run: tuple[str, str, int] | None = None
  duplicates = 0

  for line_number, raw_line in enumerate(log_path.read_text(encoding="utf-8").splitlines(), start=1):
    line = raw_line.strip()
    if not line:
      continue

    run_match = RUN_RE.match(line)
    if run_match:
      mesh = run_match.group("mesh")
      target = run_match.group("target")
      threads = int(run_match.group("threads"))
      current_run = (mesh, target, threads)
      ordered_insert(mesh_order, mesh)
      ordered_insert(target_order, target)
      ordered_insert(thread_order, threads)
      continue

    time_match = TIME_RE.match(line)
    if time_match:
      if current_run is None:
        warnings.append(f"line {line_number}: found a timing line before any Running line")
        continue
      if current_run in results:
        duplicates += 1
      results[current_run] = float(time_match.group("time"))
      current_run = None

  if current_run is not None:
    warnings.append(f"missing timing line after run: mesh={current_run[0]} target={current_run[1]} threads={current_run[2]}")
  if duplicates:
    warnings.append(f"found {duplicates} duplicate run entries; using the last timing for each duplicate")

  return results, list(mesh_order), list(target_order), sorted(thread_order), warnings


def sorted_targets(targets: list[str]) -> list[str]:
  expected_rank = {target: index for index, target in enumerate(EXPECTED_TARGETS)}
  return sorted(targets, key=lambda target: (expected_rank.get(target, len(expected_rank)), target))


def expected_warnings(results: dict[tuple[str, str, int], float]) -> list[str]:
  warnings: list[str] = []
  present_meshes = {mesh for mesh, _, _ in results}
  present_targets = {target for _, target, _ in results}
  present_threads = {threads for _, _, threads in results}

  missing_threads = [thread for thread in EXPECTED_THREADS if thread not in present_threads]
  if missing_threads:
    warnings.append(f"missing expected thread counts: {', '.join(map(str, missing_threads))}")

  missing_targets = [target for target in EXPECTED_TARGETS if target not in present_targets]
  if missing_targets:
    warnings.append(f"missing expected target percentages: {', '.join(missing_targets)}")

  missing_meshes = [mesh for mesh in EXPECTED_MESHES if mesh not in present_meshes]
  if missing_meshes:
    warnings.append(f"missing expected meshes: {', '.join(missing_meshes)}")

  missing_baselines = sorted((mesh, target) for mesh in present_meshes for target in present_targets
                             if (mesh, target, 1) not in results)
  if missing_baselines:
    preview = ", ".join(f"{mesh}/{target}" for mesh, target in missing_baselines[:8])
    suffix = "" if len(missing_baselines) <= 8 else f", ... ({len(missing_baselines)} total)"
    warnings.append(f"missing 1-thread baselines for speedups: {preview}{suffix}")

  return warnings


def coordinate_block(results: dict[tuple[str, str, int], float], mesh: str, target: str, threads: list[int]) -> str:
  baseline = results.get((mesh, target, 1))
  if baseline is None:
    return ""

  coordinates: list[str] = []
  for thread_count in threads:
    time = results.get((mesh, target, thread_count))
    if time is None:
      continue
    coordinates.append(f"({thread_count},{baseline / time:.6f})")
  return " ".join(coordinates)


def generate_latex(results: dict[tuple[str, str, int], float],
                   meshes: list[str],
                   targets: list[str],
                   threads: list[int],
                   warnings: list[str]) -> str:
  meshes = [mesh for mesh in EXPECTED_MESHES if mesh in meshes] + [mesh for mesh in meshes if mesh not in EXPECTED_MESHES]
  targets = sorted_targets(targets)
  threads = sorted(threads)
  xtick_values = threads if threads else list(EXPECTED_THREADS)
  mesh_count = max(1, len(meshes))
  horizontal_sep_fraction = 0.07 if mesh_count > 1 else 0.0
  plot_width_fraction = (0.94 - horizontal_sep_fraction * (mesh_count - 1)) / mesh_count
  plot_height_fraction = 0.72 / mesh_count

  lines: list[str] = [
      r"% Generated by generate_latex_results.py.",
      r"% Preamble requirements:",
      r"% \usepackage{pgfplots}",
      r"% \usepgfplotslibrary{groupplots}",
      r"% \pgfplotsset{compat=1.18}",
  ]
  for warning in warnings:
    lines.append(f"% WARNING: {warning}")

  lines.extend([
      "",
      r"\begin{figure}[htbp]",
      r"\centering",
      r"\noindent\makebox[\linewidth][c]{%",
      r"\begin{tikzpicture}",
      r"\begin{groupplot}[",
      f"  group style={{group size={mesh_count} by 1, horizontal sep={horizontal_sep_fraction:.4f}\\paperwidth}},",
      rf"  width={{{plot_width_fraction:.4f}\paperwidth}},",
      rf"  height={{{plot_height_fraction:.4f}\paperwidth}},",
      r"  xlabel={Threads},",
      f"  xtick={{{','.join(map(str, xtick_values))}}},",
      r"  grid=both,",
      r"]",
  ])

  added_legend_entries = False
  for mesh_index, mesh in enumerate(meshes):
    ylabel_option = ", ylabel={Speedup}" if mesh_index == 0 else ""
    legend_option = (
        r", legend style={font=\scriptsize, at={(0,1.22)}, anchor=south west, "
        r"legend columns=5, /tikz/every even column/.append style={column sep=0.35cm}}"
        if not added_legend_entries else ""
    )
    lines.append(f"\\nextgroupplot[title={{{latex_escape(mesh)}}}{ylabel_option}{legend_option}]")
    plotted_any = False
    for target in targets:
      coordinates = coordinate_block(results, mesh, target, threads)
      if not coordinates:
        continue
      lines.append(f"\\addplot+[mark=*] coordinates {{{coordinates}}};")
      if not added_legend_entries:
        lines.append(f"\\addlegendentry{{{latex_escape(target)}}}")
      plotted_any = True
    if plotted_any and not added_legend_entries:
      added_legend_entries = True
    if not plotted_any:
      lines.append(r"% No complete speedup data with a 1-thread baseline for this mesh.")

  lines.extend([
      r"\end{groupplot}",
      r"\end{tikzpicture}",
      r"}%",
      r"\caption{Mesh simplification speedup relative to the one-thread runtime.}",
      r"\label{fig:mesh-simplification-speedup}",
      r"\end{figure}",
      "",
      r"\begin{table}[htbp]",
      r"\centering",
      r"\begin{tabular}{l" + "r" * len(targets) + "}",
      r"\hline",
      "Mesh & " + " & ".join(latex_escape(target) for target in targets) + r" \\",
      r"\hline",
  ])

  for mesh in meshes:
    row = [latex_escape(mesh)]
    for target in targets:
      time = results.get((mesh, target, 1))
      row.append("--" if time is None else f"{time:.3f}")
    lines.append(" & ".join(row) + r" \\")

  lines.extend([
      r"\hline",
      r"\end{tabular}",
      r"\caption{One-thread simplification time in seconds for each mesh and target vertex percentage.}",
      r"\label{tab:mesh-simplification-one-thread-times}",
      r"\end{table}",
      "",
  ])

  return "\n".join(lines)


def main() -> int:
  parser = argparse.ArgumentParser(description="Generate LaTeX PGFPlots graphs and timing tables from run_sweep.log.")
  parser.add_argument("log", nargs="?", default="output/run_sweep.log", type=Path, help="Path to run_sweep.log")
  parser.add_argument("-o", "--output", default=Path("output/results.tex"), type=Path, help="Output .tex file")
  args = parser.parse_args()

  if not args.log.exists():
    print(f"Missing log file: {args.log}", file=sys.stderr)
    return 1

  results, meshes, targets, threads, parse_warnings = parse_log(args.log)
  warnings = parse_warnings + expected_warnings(results)
  if not results:
    print(f"No successful simplification timings found in {args.log}", file=sys.stderr)
    return 1

  tex = generate_latex(results, meshes, targets, threads, warnings)
  args.output.parent.mkdir(parents=True, exist_ok=True)
  args.output.write_text(tex, encoding="utf-8")

  print(f"Wrote {args.output}")
  print(f"Parsed {len(results)} timing entries from {args.log}")
  for warning in warnings:
    print(f"Warning: {warning}", file=sys.stderr)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
