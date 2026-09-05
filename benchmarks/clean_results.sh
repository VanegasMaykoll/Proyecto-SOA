#!/usr/bin/env bash
# ==============================================================================
# SCRIPT DE LIMPIEZA DE RESULTADOS Y DATOS TEMPORALES
# ==============================================================================

set -u
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/config.sh"

echo "Limpiando archivos de resumen en ${RESULTS_DIR}..."
rm -f "${RESULTS_DIR}"/resumen_*.txt

echo "Limpiando logs crudos en ${RAW_LOGS_DIR}..."
rm -rf "${RAW_LOGS_DIR:?}"/*
for eng in "${ENGINES[@]}"; do
    mkdir -p "${RAW_LOGS_DIR}/${eng}"
done

echo "Limpiando datos temporales en ${TMP_DATA_DIR}..."
rm -rf "${TMP_DATA_DIR:?}"/*

echo "Limpieza completada exitosamente."
