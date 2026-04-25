#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_ROOT="${OUTPUT_ROOT:-${SCRIPT_DIR}/output}"
MODES_CSV="${MODES_CSV:-sequential,partitioned_batch}"
THREAD_COUNTS_CSV="${THREAD_COUNTS_CSV:-1,2,4,8,16}"
TARGET_VERTEX_PERCENTS_CSV="${TARGET_VERTEX_PERCENTS_CSV:-25%,10%,1%,0.5%}"
MESHES_CSV="${MESHES_CSV:-bunny,buddha,dragon}"
PARTITIONS_PER_THREAD="${PARTITIONS_PER_THREAD:-1}"
BATCH_SIZE="${BATCH_SIZE:-64}"
REPARTITION_EVERY="${REPARTITION_EVERY:-0}"

DEFAULT_BINARIES=(
  "${SCRIPT_DIR}/out/build/release/src/mesh_simplification"
  "${SCRIPT_DIR}/build/src/mesh_simplification"
  "${SCRIPT_DIR}/build-debug/src/mesh_simplification"
)

pick_binary() {
  if [[ -n "${BINARY:-}" ]]; then
    printf '%s\n' "${BINARY}"
    return 0
  fi

  for candidate in "${DEFAULT_BINARIES[@]}"; do
    if [[ ! -x "${candidate}" ]]; then
      continue
    fi

    candidate_usage="$("${candidate}" 2>&1 || true)"
    if [[ "${candidate_usage}" == *"--mode MODE"* ]]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done

  printf '%s\n' "${DEFAULT_BINARIES[0]}"
}

BINARY="$(pick_binary)"
IFS=',' read -r -a MODES <<< "${MODES_CSV}"
IFS=',' read -r -a THREAD_COUNTS <<< "${THREAD_COUNTS_CSV}"
IFS=',' read -r -a TARGET_VERTEX_PERCENTS <<< "${TARGET_VERTEX_PERCENTS_CSV}"
IFS=',' read -r -a MESHES <<< "${MESHES_CSV}"

mkdir -p "${OUTPUT_ROOT}"

if [[ ! -x "${BINARY}" ]]; then
  {
    echo "Missing executable: ${BINARY}"
    echo "Build it first with: cmake --preset release && cmake --build --preset release"
  } >&2
  exit 1
fi

BINARY_USAGE="$("${BINARY}" 2>&1 || true)"
if [[ "${BINARY_USAGE}" != *"--mode MODE"* || "${BINARY_USAGE}" != *"--stats-out PATH"* ]]; then
  {
    echo "Executable appears to be stale or incompatible: ${BINARY}"
    echo "Expected usage to include: --mode MODE and --stats-out PATH"
    echo "Actual usage/output:"
    echo "${BINARY_USAGE}"
    echo "Rebuild from the current source, e.g.: cmake --build --preset release"
  } >&2
  exit 1
fi

RUNNER="$(mktemp "${OUTPUT_ROOT}/mesh_simplification_runner.XXXXXX")"
cp "${BINARY}" "${RUNNER}"
chmod +x "${RUNNER}"
trap 'rm -f "${RUNNER}"' EXIT

for mode in "${MODES[@]}"; do
  MODE_OUTPUT_DIR="${OUTPUT_ROOT}/${mode}"
  MODE_STATS_DIR="${MODE_OUTPUT_DIR}/stats"
  LOG_FILE="${MODE_OUTPUT_DIR}/run_sweep.log"
  mkdir -p "${MODE_OUTPUT_DIR}" "${MODE_STATS_DIR}"
  : > "${LOG_FILE}"

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
        output_path="${MODE_OUTPUT_DIR}/${mesh}_r${reduction_label}_t${threads}.obj"
        stats_path="${MODE_STATS_DIR}/${mesh}_r${reduction_label}_t${threads}.csv"

        partitions=$(( threads * PARTITIONS_PER_THREAD ))
        if [[ "${mode}" == "sequential" ]]; then
          partitions=1
        fi

        cmd=(
          "${RUNNER}"
          "${input_path}"
          "${output_path}"
          "${target_vertex_percent}"
          --mode "${mode}"
          --threads "${threads}"
          --stats-out "${stats_path}"
        )
        if [[ "${mode}" == "partitioned_batch" ]]; then
          cmd+=(--partitions "${partitions}" --batch-size "${BATCH_SIZE}")
          if [[ "${REPARTITION_EVERY}" != "0" ]]; then
            cmd+=(--repartition-every "${REPARTITION_EVERY}")
          fi
        fi

        {
          echo "Running mode=${mode} mesh=${mesh} target_vertices=${target_vertex_percent} threads=${threads} partitions=${partitions}"
          "${cmd[@]}"
          echo
        } 2>&1 | tee -a "${LOG_FILE}"
      done
    done
  done
done
