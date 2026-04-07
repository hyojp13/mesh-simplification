#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${BINARY:-${SCRIPT_DIR}/build/src/mesh_simplification}"
OUTPUT_DIR="${SCRIPT_DIR}/output"
LOG_FILE="${OUTPUT_DIR}/run_sweep.log"

THREAD_COUNTS=(1)
TARGET_VERTEX_PERCENTS=("25%" "10%" "1%" "0.5%")
MESHES=("bunny" "buddha" "dragon")

mkdir -p "${OUTPUT_DIR}"
: > "${LOG_FILE}"

if [[ ! -x "${BINARY}" ]]; then
  {
    echo "Missing executable: ${BINARY}"
    echo "Build it first with: cmake --build build"
  } 2>&1 | tee -a "${LOG_FILE}" >&2
  exit 1
fi

BINARY_USAGE="$("${BINARY}" 2>&1 || true)"
if [[ "${BINARY_USAGE}" != *"<target_vertex_percent>"* || "${BINARY_USAGE}" != *"[threads]"* ]]; then
  {
    echo "Executable appears to be stale or incompatible: ${BINARY}"
    echo "Expected usage to include: <target_vertex_percent> [threads]"
    echo "Actual usage/output:"
    echo "${BINARY_USAGE}"
    echo "Rebuild from the current source, e.g.: cmake --build build"
  } 2>&1 | tee -a "${LOG_FILE}" >&2
  exit 1
fi

RUNNER="$(mktemp "${OUTPUT_DIR}/mesh_simplification_runner.XXXXXX")"
cp "${BINARY}" "${RUNNER}"
chmod +x "${RUNNER}"
trap 'rm -f "${RUNNER}"' EXIT

for mesh in "${MESHES[@]}"; do
  input_path="${SCRIPT_DIR}/${mesh}.obj"

  if [[ ! -f "${input_path}" ]]; then
    echo "Missing input mesh: ${input_path}" >&2
    exit 1
  fi

  for target_vertex_percent in "${TARGET_VERTEX_PERCENTS[@]}"; do
    target_vertex_value="${target_vertex_percent%\%}"
    reduction_label="${target_vertex_value//./p}"

    for threads in "${THREAD_COUNTS[@]}"; do
      output_path="${OUTPUT_DIR}/${mesh}_r${reduction_label}_t${threads}.obj"

      echo "Running mesh=${mesh} target_vertices=${target_vertex_percent} threads=${threads}" | tee -a "${LOG_FILE}"
      "${RUNNER}" "${input_path}" "${output_path}" "${target_vertex_percent}" "${threads}" 2>&1 | tee -a "${LOG_FILE}"
      echo | tee -a "${LOG_FILE}"
    done
  done
done
