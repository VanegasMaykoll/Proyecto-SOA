#!/usr/bin/env bash
# ==============================================================================
# EJECUTOR MAESTRO DE LA SUITE DE BENCHMARKS (PROYECTO SOA)
# Ejecuta de forma secuencial las 5 pruebas en los 4 motores KV
# ==============================================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/config.sh"

echo "================================================================================"
echo "          INICIANDO SUITE COMPLETA DE BENCHMARKS PARA MOTORES KV                "
echo "================================================================================"
echo "Motores:    ${ENGINES[*]}"
echo "Variantes:  ${VARIANTS[*]}"
echo "Volumen:    NUM_KEYS=${NUM_KEYS}, NUM_READS=${NUM_READS}"
echo "Resultados: ${RESULTS_DIR}/resumen_<motor>.txt"
echo "================================================================================"
echo ""

START_TIME=$(date +%s)

# 1. Prueba 01: Negative Lookups
"${SCRIPT_DIR}/bench_01_readmissing.sh"

# 2. Prueba 02: Positive Point Reads
"${SCRIPT_DIR}/bench_02_readrandom.sh"

# 3. Prueba 03: Ingesta Masiva y Huella en Disco
"${SCRIPT_DIR}/bench_03_fillrandom_space.sh"

# 4. Prueba 04: Lectura Concurrente con Escritura Activa
"${SCRIPT_DIR}/bench_04_readwhilewriting.sh"

# 5. Prueba 05: Búsquedas de Rango / Iteradores
"${SCRIPT_DIR}/bench_05_seekrandom.sh"

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "================================================================================"
echo " SUITE DE BENCHMARKS FINALIZADA EN ${ELAPSED} SEGUNDOS."
echo "================================================================================"
echo "Archivos de resumen generados:"
for engine in "${ENGINES[@]}"; do
    FILE="$(get_summary_file "$engine")"
    if [[ -f "$FILE" ]]; then
        echo "  - ${engine^^}: $FILE"
    fi
done
echo "Logs detallados disponibles en: ${RAW_LOGS_DIR}/"
echo "================================================================================"
