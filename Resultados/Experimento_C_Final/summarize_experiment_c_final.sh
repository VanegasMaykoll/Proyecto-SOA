#!/usr/bin/env bash
set -u

ROOT="/home/maykoll_vanegas/Proyecto-SOA-local/Resultados/Experimento_C_Final"
RUN="$ROOT/run_01"
OUT="$RUN/resumen_metricas.txt"

: > "$OUT"

echo "============================================================" | tee -a "$OUT"
echo " EXPERIMENTO C - PEBBLESDB - RESUMEN FINAL" | tee -a "$OUT"
echo "============================================================" | tee -a "$OUT"
echo | tee -a "$OUT"

for variant in nofilter bloom ribbon xor; do
    BASE="$RUN/$variant"
    LOGS="$BASE/logs"

    echo "------------------------------------------------------------" | tee -a "$OUT"
    echo "VARIANTE: $variant" | tee -a "$OUT"
    echo "------------------------------------------------------------" | tee -a "$OUT"

    for f in \
        01_fillrandom.log \
        02_readrandom_validation.log \
        03_readmissing.log \
        04_readrandom.log; do

        LOG="$LOGS/$f"

        if [[ ! -f "$LOG" ]]; then
            echo "$f: NO ENCONTRADO" | tee -a "$OUT"
            continue
        fi

        echo | tee -a "$OUT"
        echo "[$f]" | tee -a "$OUT"

        grep -E \
          "^Filter:|^FilterMode:|^FilterBits:|fillrandom|readrandom|readmissing" \
          "$LOG" \
          | tee -a "$OUT"

        grep -E \
          "Elapsed \(wall clock\)|Maximum resident set size|File system inputs|File system outputs|Exit status" \
          "$LOG" \
          | tee -a "$OUT"
    done

    echo | tee -a "$OUT"

    if [[ -f "$BASE/metadata/size_after_fillrandom.txt" ]]; then
        echo -n "DB bytes after fillrandom: " | tee -a "$OUT"
        awk '{print $1}' "$BASE/metadata/size_after_fillrandom.txt" | tee -a "$OUT"
    fi

    if [[ -f "$BASE/metadata/size_final.txt" ]]; then
        echo -n "DB bytes final: " | tee -a "$OUT"
        awk '{print $1}' "$BASE/metadata/size_final.txt" | tee -a "$OUT"
    fi

    echo | tee -a "$OUT"
done

echo "============================================================" | tee -a "$OUT"
echo "VALIDACION DE CORRECCION" | tee -a "$OUT"
echo "============================================================" | tee -a "$OUT"

for variant in nofilter bloom ribbon xor; do
    LOG="$RUN/$variant/logs/02_readrandom_validation.log"

    printf "%-10s : " "$variant" | tee -a "$OUT"

    if [[ ! -f "$LOG" ]]; then
        echo "SIN RESULTADO" | tee -a "$OUT"
    elif grep -q "1000000 of 1000000 found" "$LOG"; then
        echo "OK - 1000000/1000000" | tee -a "$OUT"
    else
        echo "INVALIDO" | tee -a "$OUT"
    fi
done

echo | tee -a "$OUT"
echo "Resumen guardado en:" | tee -a "$OUT"
echo "$OUT" | tee -a "$OUT"
