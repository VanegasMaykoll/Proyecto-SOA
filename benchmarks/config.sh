#!/usr/bin/env bash
# ==============================================================================
# PROYECTO SOA - CONFIGURACIÓN CENTRAL DE BENCHMARKS
# ==============================================================================

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINES_DIR="${PROJECT_ROOT}/engines"
RESULTS_DIR="${PROJECT_ROOT}/results"
RAW_LOGS_DIR="${RESULTS_DIR}/raw_logs"
REPORTER="${PROJECT_ROOT}/benchmarks/reporter.py"
TMP_DATA_DIR="/tmp/soa_kv_benchmark_data"

# Binarios db_bench por motor
BIN_LEVELDB="${ENGINES_DIR}/leveldb-1.23/build/db_bench"
BIN_PEBBLESDB="${ENGINES_DIR}/pebblesdb-v1.0/db_bench"
BIN_ROCKSDB="${ENGINES_DIR}/rocksdb-8.6.7/db_bench"
BIN_SPEEDB="${ENGINES_DIR}/speedb-2.8.0/db_bench"

ENGINES=("leveldb" "pebblesdb" "rocksdb" "speedb")
VARIANTS=("nofilter" "bloom" "ribbon" "xor")

# Parámetros configurables por variables de entorno
NUM_KEYS="${NUM_KEYS:-250000}"
NUM_READS="${NUM_READS:-$NUM_KEYS}"
VALUE_SIZE="${VALUE_SIZE:-100}"
WRITE_BUFFER_SIZE="${WRITE_BUFFER_SIZE:-4194304}"  # 4 MB
CACHE_SIZE="${CACHE_SIZE:-33554432}"               # 32 MB
BLOCK_SIZE="${BLOCK_SIZE:-4096}"                   # 4 KB
COMPRESSION_RATIO="${COMPRESSION_RATIO:-0.5}"

# Crear directorios si no existen
mkdir -p "${RESULTS_DIR}" "${RAW_LOGS_DIR}" "${TMP_DATA_DIR}"
for eng in "${ENGINES[@]}"; do
    mkdir -p "${RAW_LOGS_DIR}/${eng}"
done

get_engine_bin() {
    local engine="$1"
    case "$engine" in
        leveldb)   echo "$BIN_LEVELDB" ;;
        pebblesdb) echo "$BIN_PEBBLESDB" ;;
        rocksdb)   echo "$BIN_ROCKSDB" ;;
        speedb)    echo "$BIN_SPEEDB" ;;
        *) echo "Error: motor desconocido '$engine'" >&2; return 1 ;;
    esac
}

get_summary_file() {
    local engine="$1"
    echo "${RESULTS_DIR}/resumen_${engine}.txt"
}

get_variant_flags() {
    local variant="$1"
    case "$variant" in
        nofilter)
            echo "--bloom_bits=-1 --use_ribbon_filter=0 --use_xor_filter=0"
            ;;
        bloom)
            echo "--bloom_bits=10 --use_ribbon_filter=0 --use_xor_filter=0"
            ;;
        ribbon)
            echo "--bloom_bits=10 --use_ribbon_filter=1 --use_xor_filter=0"
            ;;
        xor)
            echo "--bloom_bits=10 --use_ribbon_filter=0 --use_xor_filter=1"
            ;;
        *)
            echo "Error: variante desconocida '$variant'" >&2
            return 1
            ;;
    esac
}

init_summary_if_needed() {
    local engine="$1"
    local summary_file="$(get_summary_file "$engine")"
    local params="N=${NUM_KEYS}, Reads=${NUM_READS}, ValueSize=${VALUE_SIZE}B, WriteBuffer=4MB, Cache=32MB"
    "${REPORTER}" init-engine "$engine" "$summary_file" "$params"
}
