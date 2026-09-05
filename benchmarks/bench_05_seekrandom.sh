#!/usr/bin/env bash
# ==============================================================================
# PRUEBA 05: BÚSQUEDAS DE RANGO / ITERADORES (seekrandom)
# Evalúa el comportamiento de los filtros ante operaciones Seek(key) de cursores
# e iteradores en bloques de datos.
# ==============================================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/config.sh"

TEST_ID="05"
TEST_NAME="Búsquedas de Rango / Iteradores (seekrandom)"
TEST_DESC="Posicionamiento de iteradores y cursores Seek sobre bloques de datos."
TARGET_OP="seekrandom"

# Para seekrandom usamos típicamente el 20% de N para mantener tiempos razonables
NUM_SEEKS="${SEEK_OPS:-$((NUM_KEYS / 5))}"
if [[ "$NUM_SEEKS" -lt 100 ]]; then
    NUM_SEEKS=100
fi

echo "================================================================================"
echo " EJECUTANDO PRUEBA ${TEST_ID}: ${TEST_NAME}"
echo " Operaciones: N=${NUM_KEYS} writes, Seeks=${NUM_SEEKS} seeks aleatorios"
echo "================================================================================"

for engine in "${ENGINES[@]}"; do
    BIN="$(get_engine_bin "$engine")"
    SUMMARY_FILE="$(get_summary_file "$engine")"

    if [[ ! -x "$BIN" ]]; then
        echo "Saltando $engine: binario '$BIN' no encontrado o no ejecutable."
        continue
    fi

    echo ""
    echo ">>> Motor: ${engine^^} <<<"
    init_summary_if_needed "$engine"
    "${REPORTER}" start-test "$SUMMARY_FILE" "$TEST_ID" "$TEST_NAME" "$TEST_DESC"

    for variant in "${VARIANTS[@]}"; do
        FLAGS="$(get_variant_flags "$variant")"
        DB_DIR="${TMP_DATA_DIR}/${engine}_${TEST_ID}_${variant}"
        LOG_FILE="${RAW_LOGS_DIR}/${engine}/${TEST_ID}_seekrandom_${variant}.log"

        echo "  -> Ejecutando variante: ${variant}..."
        rm -rf "$DB_DIR"
        mkdir -p "$DB_DIR"

        # Ejecutar db_bench con fillrandom,seekrandom
        # shellcheck disable=SC2086
        "${BIN}" \
            --db="${DB_DIR}" \
            --benchmarks="fillrandom,seekrandom" \
            --num="${NUM_KEYS}" \
            --reads="${NUM_SEEKS}" \
            --value_size="${VALUE_SIZE}" \
            --write_buffer_size="${WRITE_BUFFER_SIZE}" \
            --cache_size="${CACHE_SIZE}" \
            --block_size="${BLOCK_SIZE}" \
            --compression_ratio="${COMPRESSION_RATIO}" \
            ${FLAGS} > "${LOG_FILE}" 2>&1 || {
                echo "     [ERROR] Falló la ejecución para ${engine} / ${variant}. Ver log: ${LOG_FILE}"
            }

        # Registrar fila de resultados
        "${REPORTER}" add-row "$SUMMARY_FILE" "$variant" "$LOG_FILE" "$TARGET_OP" "$DB_DIR"

        # Limpiar base de datos temporal
        rm -rf "$DB_DIR"
    done

    "${REPORTER}" end-test "$SUMMARY_FILE"
    echo "Resultados para ${engine^^} guardados en: ${SUMMARY_FILE}"
done

echo ""
echo "================================================================================"
echo " PRUEBA ${TEST_ID} COMPLETADA PARA TODOS LOS MOTORES."
echo "================================================================================"
