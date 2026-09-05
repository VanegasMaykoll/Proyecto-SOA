#!/usr/bin/env bash
# ==============================================================================
# PRUEBA 03: INGESTA MASIVA Y HUELLA EN DISCO (fillrandom masivo)
# Evalúa el costo de construcción de los filtros durante compactaciones de fondo
# y el tamaño real en disco de la base de datos (reducción de espacio SST).
# ==============================================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/config.sh"

TEST_ID="03"
TEST_NAME="Ingesta Masiva y Huella en Disco (fillrandom)"
TEST_DESC="Escritura masiva de 1KB forzando compactaciones y evaluando tamaño en disco."
TARGET_OP="fillrandom"

# Para esta prueba usamos valores de 1024 bytes para generar suficiente volumen
VALUE_SIZE_P3="${FILL_VALUE_SIZE:-1024}"

echo "================================================================================"
echo " EJECUTANDO PRUEBA ${TEST_ID}: ${TEST_NAME}"
echo " Operaciones: N=${NUM_KEYS} writes masivos (Valor=${VALUE_SIZE_P3}B)"
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
        LOG_FILE="${RAW_LOGS_DIR}/${engine}/${TEST_ID}_fillrandom_${variant}.log"

        echo "  -> Ejecutando variante: ${variant}..."
        rm -rf "$DB_DIR"
        mkdir -p "$DB_DIR"

        # Ejecutar db_bench con fillrandom
        # shellcheck disable=SC2086
        "${BIN}" \
            --db="${DB_DIR}" \
            --benchmarks="fillrandom" \
            --num="${NUM_KEYS}" \
            --value_size="${VALUE_SIZE_P3}" \
            --write_buffer_size="${WRITE_BUFFER_SIZE}" \
            --cache_size="${CACHE_SIZE}" \
            --block_size="${BLOCK_SIZE}" \
            --compression_ratio="${COMPRESSION_RATIO}" \
            ${FLAGS} > "${LOG_FILE}" 2>&1 || {
                echo "     [ERROR] Falló la ejecución para ${engine} / ${variant}. Ver log: ${LOG_FILE}"
            }

        # Registrar fila de resultados (incluyendo tamaño de DB en disco)
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
