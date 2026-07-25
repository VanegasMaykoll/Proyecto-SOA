# LevelDB 1.23 — Evaluación de Bloom Filter con `db_bench`

Este documento describe el procedimiento utilizado para evaluar **LevelDB 1.23** con su benchmark nativo `db_bench`, comparando:

- **Sin filtro Bloom**
- **Bloom Filter con 10 bits por clave**

Las pruebas ejecutadas son:

1. `fillrandom`: escrituras aleatorias.
2. `compact`: compactación completa.
3. `readmissing`: consultas de claves inexistentes.
4. `readrandom`: consultas de claves existentes.
5. `stats,sstables`: estadísticas estructurales.

También se guardan los logs completos, histogramas, comparaciones de claves, uso de CPU, memoria máxima, actividad del sistema de archivos y tamaño final de las bases.

---

## 1. Requisitos previos

Se asume que LevelDB ya está compilado y que existe:

```text
~/kv_engines_clean/bin/leveldb_db_bench
```

Estructura esperada:

```text
kv_engines_clean/
├── bin/
│   └── leveldb_db_bench
├── data/
├── engines/
│   └── leveldb-1.23/
└── results/
```

Verificar el ejecutable:

```bash
export KVROOT="$HOME/kv_engines_clean"

ls -lh "$KVROOT/bin/leveldb_db_bench"
readlink -f "$KVROOT/bin/leveldb_db_bench"
```

Verificar `/usr/bin/time`:

```bash
/usr/bin/time --version | head -n 1
```

Instalarlo si no está disponible:

```bash
sudo apt update
sudo apt install -y time
```

---

## 2. Verificar la versión

```bash
git -C "$KVROOT/engines/leveldb-1.23" describe --tags --always
git -C "$KVROOT/engines/leveldb-1.23" rev-parse HEAD
git -C "$KVROOT/engines/leveldb-1.23" status
```

Versión utilizada:

```text
LevelDB 1.23
Commit: 99b3c03b3284f5886f9ef9a4ef703d57373e61be
```

---

## 3. Configuración experimental

```bash
export KVROOT="$HOME/kv_engines_clean"

export NUM=1000000
export READS=1000000
export VALUE_SIZE=1024
export THREADS=1

export CACHE_SIZE=$((32 * 1024 * 1024))
export WRITE_BUFFER_SIZE=$((4 * 1024 * 1024))

export BLOOM_BITS=10
export COMPRESSION_RATIO=1.0

mkdir -p "$KVROOT/data/stage1"
mkdir -p "$KVROOT/results/stage1"

set -o pipefail
```

Comprobar valores:

```bash
printf '%s\n' \
  "KVROOT=$KVROOT" \
  "NUM=$NUM" \
  "READS=$READS" \
  "VALUE_SIZE=$VALUE_SIZE" \
  "THREADS=$THREADS" \
  "CACHE_SIZE=$CACHE_SIZE" \
  "WRITE_BUFFER_SIZE=$WRITE_BUFFER_SIZE" \
  "BLOOM_BITS=$BLOOM_BITS" \
  "COMPRESSION_RATIO=$COMPRESSION_RATIO"
```

Parámetros:

| Parámetro | Valor |
|---|---:|
| Registros | 1 000 000 |
| Lecturas | 1 000 000 |
| Tamaño del valor | 1024 bytes |
| Hilos | 1 |
| Block cache | 32 MiB |
| Write buffer | 4 MiB |
| Bloom Filter | 10 bits por clave |
| Compression ratio | 1.0 |

En LevelDB:

```text
--bloom_bits=-1   → filtro desactivado
--bloom_bits=10   → Bloom Filter con 10 bits por clave
```

---

## 4. Prueba rápida opcional

```bash
rm -rf "$KVROOT/data/smoke_leveldb"

"$KVROOT/bin/leveldb_db_bench" \
  --benchmarks=fillseq \
  --num=10000 \
  --value_size=100 \
  --threads=1 \
  --db="$KVROOT/data/smoke_leveldb"
```

---

## 5. Ejecutar el experimento completo

Este bloque ejecuta las condiciones `nofilter` y `bloom10`.

```bash
BIN="$KVROOT/bin/leveldb_db_bench"

for MODE in nofilter bloom10; do

  if [ "$MODE" = "nofilter" ]; then
    BITS=-1
  else
    BITS="$BLOOM_BITS"
  fi

  DB="$KVROOT/data/stage1/leveldb_${MODE}"
  OUT="$KVROOT/results/stage1/leveldb_${MODE}"

  echo
  echo "=================================================="
  echo "LevelDB 1.23"
  echo "Condición: $MODE"
  echo "bloom_bits=$BITS"
  echo "Base: $DB"
  echo "=================================================="

  rm -rf "$DB"

  # 1. Escrituras aleatorias
  /usr/bin/time -v \
    -o "${OUT}_fillrandom.time" \
    "$BIN" \
      --benchmarks=fillrandom \
      --num="$NUM" \
      --value_size="$VALUE_SIZE" \
      --threads="$THREADS" \
      --cache_size="$CACHE_SIZE" \
      --write_buffer_size="$WRITE_BUFFER_SIZE" \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --comparisons=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_fillrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante fillrandom de LevelDB: $MODE"
    exit 1
  fi

  # 2. Compactación
  /usr/bin/time -v \
    -o "${OUT}_compact.time" \
    "$BIN" \
      --benchmarks=compact \
      --use_existing_db=1 \
      --num="$NUM" \
      --value_size="$VALUE_SIZE" \
      --threads="$THREADS" \
      --cache_size="$CACHE_SIZE" \
      --write_buffer_size="$WRITE_BUFFER_SIZE" \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --db="$DB" \
    2>&1 | tee "${OUT}_compact.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante compact de LevelDB: $MODE"
    exit 1
  fi

  # 3. Consultas de claves inexistentes
  /usr/bin/time -v \
    -o "${OUT}_readmissing.time" \
    "$BIN" \
      --benchmarks=readmissing \
      --use_existing_db=1 \
      --num="$NUM" \
      --reads="$READS" \
      --value_size="$VALUE_SIZE" \
      --threads="$THREADS" \
      --cache_size="$CACHE_SIZE" \
      --write_buffer_size="$WRITE_BUFFER_SIZE" \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --comparisons=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readmissing.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readmissing de LevelDB: $MODE"
    exit 1
  fi

  # 4. Consultas de claves existentes
  /usr/bin/time -v \
    -o "${OUT}_readrandom.time" \
    "$BIN" \
      --benchmarks=readrandom \
      --use_existing_db=1 \
      --num="$NUM" \
      --reads="$READS" \
      --value_size="$VALUE_SIZE" \
      --threads="$THREADS" \
      --cache_size="$CACHE_SIZE" \
      --write_buffer_size="$WRITE_BUFFER_SIZE" \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --comparisons=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readrandom de LevelDB: $MODE"
    exit 1
  fi

  # 5. Estadísticas y SSTables
  "$BIN" \
    --benchmarks=stats,sstables \
    --use_existing_db=1 \
    --num="$NUM" \
    --cache_size="$CACHE_SIZE" \
    --write_buffer_size="$WRITE_BUFFER_SIZE" \
    --compression_ratio="$COMPRESSION_RATIO" \
    --bloom_bits="$BITS" \
    --db="$DB" \
    2>&1 | tee "${OUT}_db_stats.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error obteniendo estadísticas de LevelDB: $MODE"
    exit 1
  fi

  # 6. Tamaño real del directorio
  du -sb "$DB" | tee "${OUT}_database_size.txt"

done
```

---

## 6. Verificar resultados

```bash
find "$KVROOT/results/stage1" \
  -maxdepth 1 \
  -type f \
  -name "leveldb_*" \
  | sort
```

---

## 7. Consultar resultados principales

```bash
grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom|Comparisons" \
  "$KVROOT/results/stage1"/leveldb_*.log
```

Solo `readmissing`:

```bash
grep -HE \
  "^readmissing|Comparisons" \
  "$KVROOT/results/stage1"/leveldb_*_readmissing.log
```

Solo `readrandom`:

```bash
grep -HE \
  "^readrandom|Comparisons" \
  "$KVROOT/results/stage1"/leveldb_*_readrandom.log
```

Tamaño de las bases:

```bash
cat "$KVROOT/results/stage1"/leveldb_*_database_size.txt
```

---

## 8. Crear `leveldb_summary.txt`

```bash
LEVELDB_SUMMARY="$KVROOT/results/stage1/leveldb_summary.txt"

{
  echo "============================================================"
  echo "LEVELDB 1.23 - BLOOM FILTER STAGE 1"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  echo "=== VERSIÓN ==="
  echo "Etiqueta: $(git -C "$KVROOT/engines/leveldb-1.23" describe --tags --always)"
  echo "Commit: $(git -C "$KVROOT/engines/leveldb-1.23" rev-parse HEAD)"
  echo

  echo "=== CONFIGURACIÓN ==="
  echo "NUM=$NUM"
  echo "READS=$READS"
  echo "VALUE_SIZE=$VALUE_SIZE"
  echo "THREADS=$THREADS"
  echo "CACHE_SIZE=$CACHE_SIZE bytes"
  echo "WRITE_BUFFER_SIZE=$WRITE_BUFFER_SIZE bytes"
  echo "COMPRESSION_RATIO=$COMPRESSION_RATIO"
  echo "Sin filtro: bloom_bits=-1"
  echo "Con Bloom: bloom_bits=$BLOOM_BITS"
  echo "HISTOGRAM=1"
  echo "COMPARISONS=1"
  echo

  echo "=== RESULTADOS PRINCIPALES ==="

  grep -HE \
    "^fillrandom|^compact|^readmissing|^readrandom|Comparisons" \
    "$KVROOT/results/stage1"/leveldb_*.log

  echo
  echo "=== TAMAÑO DE LAS BASES ==="

  for FILE in \
    "$KVROOT/results/stage1/leveldb_nofilter_database_size.txt" \
    "$KVROOT/results/stage1/leveldb_bloom10_database_size.txt"
  do
    if [ -f "$FILE" ]; then
      echo "--- $(basename "$FILE")"
      cat "$FILE"
    fi
  done

  echo
  echo "=== ESTADÍSTICAS ESTRUCTURALES ==="

  for FILE in \
    "$KVROOT/results/stage1/leveldb_nofilter_db_stats.log" \
    "$KVROOT/results/stage1/leveldb_bloom10_db_stats.log"
  do
    if [ -f "$FILE" ]; then
      echo
      echo "---------------- $(basename "$FILE") ----------------"
      cat "$FILE"
    fi
  done

} | tee "$LEVELDB_SUMMARY"
```

Abrir:

```bash
less "$LEVELDB_SUMMARY"
```

---

## 9. Crear `leveldb_resources_summary.txt`

```bash
LEVELDB_RESOURCES="$KVROOT/results/stage1/leveldb_resources_summary.txt"

{
  echo "============================================================"
  echo "LEVELDB 1.23 - RESOURCE METRICS"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  for FILE in "$KVROOT/results/stage1"/leveldb_*.time; do

    [ -f "$FILE" ] || continue

    echo
    echo "------------------------------------------------------------"
    echo "ARCHIVO: $(basename "$FILE")"
    echo "------------------------------------------------------------"

    grep -E \
      "Command being timed|User time|System time|Percent of CPU|Elapsed \(wall clock\)|Maximum resident set size|Average resident set size|Major.*page faults|Minor.*page faults|Voluntary context switches|Involuntary context switches|File system inputs|File system outputs|Socket messages sent|Socket messages received|Exit status" \
      "$FILE"

  done

} | tee "$LEVELDB_RESOURCES"
```

Abrir:

```bash
less "$LEVELDB_RESOURCES"
```

---

## 10. Crear informe combinado

```bash
LEVELDB_REPORT="$KVROOT/results/stage1/leveldb_stage1_report.txt"

{
  cat "$KVROOT/results/stage1/leveldb_summary.txt"

  echo
  echo
  echo "############################################################"
  echo "# MÉTRICAS DE RECURSOS"
  echo "############################################################"
  echo

  cat "$KVROOT/results/stage1/leveldb_resources_summary.txt"

} > "$LEVELDB_REPORT"
```

Abrir:

```bash
less "$LEVELDB_REPORT"
```

---

## 11. Resumen rápido

```bash
echo "=== LEVELDB: RENDIMIENTO ==="

grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom|Comparisons" \
  "$KVROOT/results/stage1"/leveldb_*.log

echo
echo "=== LEVELDB: RAM MÁXIMA ==="

grep -H \
  "Maximum resident set size" \
  "$KVROOT/results/stage1"/leveldb_*.time

echo
echo "=== LEVELDB: TAMAÑO DE BASES ==="

cat "$KVROOT/results/stage1"/leveldb_*_database_size.txt
```

---

## 12. Interpretación general

| Métrica | Interpretación |
|---|---|
| `micros/op` | Latencia promedio. Menor es mejor. |
| `MB/s` | Throughput. Mayor es mejor. |
| `Comparisons` | Comparaciones internas de claves. |
| `Maximum resident set size` | Máxima RAM utilizada. |
| `File system inputs` | Entradas del sistema de archivos. |
| `File system outputs` | Salidas del sistema de archivos. |
| `database_size` | Tamaño real de la base. |

La comparación principal es:

```text
readmissing sin Bloom
           frente a
readmissing con Bloom de 10 bits
```

También deben analizarse:

- `fillrandom`, para medir el costo de construcción del filtro.
- `readrandom`, para estudiar búsquedas exitosas.
- `compact`, para observar el estado estructural de la base.

---

## 13. Archivos finales

```text
results/stage1/
├── leveldb_summary.txt
├── leveldb_resources_summary.txt
├── leveldb_stage1_report.txt
├── leveldb_nofilter_*.log
├── leveldb_nofilter_*.time
├── leveldb_bloom10_*.log
└── leveldb_bloom10_*.time
```

---

## 14. Recomendaciones

- Ejecutar las pruebas en el mismo equipo.
- Mantener el equipo conectado a corriente.
- Evitar aplicaciones pesadas durante el benchmark.
- Usar las mismas variables para todos los motores.
- Ejecutar varias repeticiones antes de obtener conclusiones.
- Conservar los archivos `.log`, `.time` y los hashes de Git.
- No modificar el motor durante esta primera etapa.
