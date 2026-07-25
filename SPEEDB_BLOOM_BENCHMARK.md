# Speedb 2.8.0 — Evaluación de Bloom Filter con `db_bench`

Este documento describe el procedimiento utilizado para evaluar **Speedb 2.8.0** mediante su benchmark nativo `db_bench`, comparando dos condiciones:

- **Sin filtro Bloom**
- **Bloom Filter con 10 bits por clave**

Las pruebas ejecutadas son:

1. `fillrandom`: creación de la base mediante escrituras aleatorias.
2. `compact`: compactación completa.
3. `readmissing`: consultas de claves inexistentes.
4. `readrandom`: consultas aleatorias de claves existentes.
5. `stats,levelstats,memstats,sstables`: estadísticas internas y estructurales.

También se almacenan:

- Salida completa de `db_bench`.
- Histogramas de latencia.
- Operaciones por segundo.
- Contadores internos de Bloom Filter.
- Métricas de block cache.
- Estadísticas de lectura, escritura y compactación.
- Información por niveles y SSTables.
- Tiempo total y tiempo de CPU.
- Consumo máximo de memoria.
- Page faults.
- Cambios de contexto.
- Entradas y salidas del sistema de archivos.
- Tamaño final de cada base de datos.

---

## 1. Requisitos previos

Este procedimiento supone que Speedb 2.8.0 ya está descargado y compilado en modo optimizado, y que existe el siguiente ejecutable:

```text
~/kv_engines_clean/bin/speedb_db_bench
```

La estructura esperada es:

```text
kv_engines_clean/
├── bin/
│   └── speedb_db_bench
├── data/
├── engines/
│   └── speedb-2.8.0/
└── results/
```

Definir la ruta raíz:

```bash
export KVROOT="$HOME/kv_engines_clean"
```

Verificar el ejecutable:

```bash
ls -lh "$KVROOT/bin/speedb_db_bench"
readlink -f "$KVROOT/bin/speedb_db_bench"
file "$KVROOT/bin/speedb_db_bench"
```

Verificar `/usr/bin/time`:

```bash
/usr/bin/time --version | head -n 1
```

Si no está instalado:

```bash
sudo apt update
sudo apt install -y time
```

---

## 2. Verificar la versión de Speedb

```bash
git -C "$KVROOT/engines/speedb-2.8.0" describe --tags --always
git -C "$KVROOT/engines/speedb-2.8.0" rev-parse HEAD
git -C "$KVROOT/engines/speedb-2.8.0" status
```

Versión utilizada:

```text
Speedb speedb/v2.8.0
Commit: c328f0ebc33db0166cd2a2844ff44c8034e30e5b
```

Speedb debe estar compilado en modo optimizado:

```text
DEBUG_LEVEL=0
```

Para reconstruir `db_bench`:

```bash
cd "$KVROOT/engines/speedb-2.8.0"

make clean

make -j"$(nproc)" \
  DEBUG_LEVEL=0 \
  db_bench
```

Actualizar el enlace simbólico:

```bash
ln -sfn \
  "$KVROOT/engines/speedb-2.8.0/db_bench" \
  "$KVROOT/bin/speedb_db_bench"
```

---

## 3. Configuración experimental

Ejecutar estas variables antes de comenzar:

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
export SEED=20260724

mkdir -p "$KVROOT/data/stage1"
mkdir -p "$KVROOT/results/stage1"

set -o pipefail
```

Comprobar los valores:

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
  "COMPRESSION_RATIO=$COMPRESSION_RATIO" \
  "SEED=$SEED"
```

### Parámetros utilizados

| Parámetro | Valor |
|---|---:|
| Registros | 1 000 000 |
| Lecturas | 1 000 000 |
| Tamaño del valor | 1024 bytes |
| Hilos | 1 |
| Block cache | 32 MiB |
| Write buffer / memtable | 4 MiB |
| Bloom Filter | 10 bits por clave |
| Compression ratio | 1.0 |
| Semilla | 20260724 |
| Memtable | SkipList |
| Compresión | Snappy |

En Speedb:

```text
--bloom_bits=0    → filtro desactivado
--bloom_bits=10   → Bloom Filter con 10 bits por clave
```

---

## 4. Configuración controlada del motor

Speedb utiliza configuraciones propias que pueden diferenciarlo de RocksDB. Para aislar mejor el efecto del filtro Bloom se fijan explícitamente las siguientes opciones:

```bash
--memtablerep=skip_list
--enable_speedb_features=0
--use_spdb_writes=0
--memtable_bloom_size_ratio=0
--whole_key_filtering=1
--use_ribbon_filter=0
```

Interpretación:

| Opción | Propósito |
|---|---|
| `memtablerep=skip_list` | Utiliza SkipList en lugar de la memtable `hash_spdb`. |
| `enable_speedb_features=0` | Evita habilitar automáticamente optimizaciones adicionales de Speedb. |
| `use_spdb_writes=0` | Desactiva el flujo de escrituras específico de Speedb. |
| `memtable_bloom_size_ratio=0` | Desactiva un posible filtro Bloom adicional en la memtable. |
| `whole_key_filtering=1` | Aplica el filtro sobre claves completas. |
| `use_ribbon_filter=0` | Fuerza Bloom tradicional y evita Ribbon Filter. |

Esto permite comparar:

```text
Speedb + SkipList + sin filtro
Speedb + SkipList + Bloom de 10 bits
```

sin cambiar simultáneamente la memtable, el tipo de filtro o el flujo de escrituras.

> Esta es una comparación controlada. No representa el rendimiento máximo de Speedb con todas sus optimizaciones nativas habilitadas.

---

## 5. Métricas internas habilitadas

Durante las pruebas se utilizan:

```bash
--histogram=1
--statistics=1
--stats_level=3
--perf_level=2
```

Interpretación:

| Opción | Propósito |
|---|---|
| `histogram=1` | Imprime la distribución de latencias. |
| `statistics=1` | Activa tickers e histogramas internos. |
| `stats_level=3` | Activa estadísticas detalladas sin todos los temporizadores más costosos. |
| `perf_level=2` | Activa contadores de `PerfContext`. |

Todas las condiciones deben ejecutarse con la misma instrumentación.

---

## 6. Prueba rápida opcional

```bash
rm -rf "$KVROOT/data/smoke_speedb"

"$KVROOT/bin/speedb_db_bench" \
  --benchmarks=fillseq \
  --num=10000 \
  --value_size=100 \
  --threads=1 \
  --memtablerep=skip_list \
  --enable_speedb_features=0 \
  --use_spdb_writes=0 \
  --db="$KVROOT/data/smoke_speedb"
```

La salida esperada debe incluir:

```text
Speedb: version 2.8
Memtablerep: SkipListFactory
fillseq : ... micros/op ... ops/sec ... MB/s
```

Speedb también puede imprimir:

```text
BEFORE Benchmark (...)
AFTER Benchmark (...)
```

Estos mensajes son parte de su instrumentación y no representan errores.

---

## 7. Ejecutar el experimento completo

Este bloque ejecuta ambas condiciones:

- `nofilter`: sin Bloom Filter.
- `bloom10`: Bloom Filter con 10 bits por clave.

Cada prueba genera:

- Un archivo `.log` con la salida completa de `db_bench`.
- Un archivo `.time` con las métricas de `/usr/bin/time -v`.

```bash
BIN="$KVROOT/bin/speedb_db_bench"

for MODE in nofilter bloom10; do

  if [ "$MODE" = "nofilter" ]; then
    BITS=0
  else
    BITS="$BLOOM_BITS"
  fi

  DB="$KVROOT/data/stage1/speedb_${MODE}"
  OUT="$KVROOT/results/stage1/speedb_${MODE}"

  echo
  echo "=================================================="
  echo "Speedb 2.8.0"
  echo "Condición: $MODE"
  echo "bloom_bits=$BITS"
  echo "Base: $DB"
  echo "=================================================="

  # Cada condición comienza con una base nueva.
  rm -rf "$DB"

  # ------------------------------------------------
  # 1. ESCRITURAS ALEATORIAS
  # ------------------------------------------------
  /usr/bin/time -v \
    -o "${OUT}_fillrandom.time" \
    "$BIN" \
      --benchmarks=fillrandom \
      --num="$NUM" \
      --value_size="$VALUE_SIZE" \
      --threads="$THREADS" \
      --cache_size="$CACHE_SIZE" \
      --write_buffer_size="$WRITE_BUFFER_SIZE" \
      --compression_type=snappy \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --use_ribbon_filter=0 \
      --whole_key_filtering=1 \
      --memtable_bloom_size_ratio=0 \
      --memtablerep=skip_list \
      --enable_speedb_features=0 \
      --use_spdb_writes=0 \
      --seed="$SEED" \
      --histogram=1 \
      --statistics=1 \
      --stats_level=3 \
      --perf_level=2 \
      --db="$DB" \
    2>&1 | tee "${OUT}_fillrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante fillrandom de Speedb: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 2. COMPACTACIÓN COMPLETA
  # ------------------------------------------------
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
      --compression_type=snappy \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --use_ribbon_filter=0 \
      --whole_key_filtering=1 \
      --memtable_bloom_size_ratio=0 \
      --memtablerep=skip_list \
      --enable_speedb_features=0 \
      --use_spdb_writes=0 \
      --seed="$SEED" \
      --statistics=1 \
      --stats_level=3 \
      --perf_level=2 \
      --db="$DB" \
    2>&1 | tee "${OUT}_compact.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante compact de Speedb: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 3. CONSULTAS DE CLAVES INEXISTENTES
  # ------------------------------------------------
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
      --compression_type=snappy \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --use_ribbon_filter=0 \
      --whole_key_filtering=1 \
      --memtable_bloom_size_ratio=0 \
      --memtablerep=skip_list \
      --enable_speedb_features=0 \
      --use_spdb_writes=0 \
      --seed="$SEED" \
      --histogram=1 \
      --statistics=1 \
      --stats_level=3 \
      --perf_level=2 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readmissing.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readmissing de Speedb: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 4. CONSULTAS ALEATORIAS DE CLAVES EXISTENTES
  # ------------------------------------------------
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
      --compression_type=snappy \
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --use_ribbon_filter=0 \
      --whole_key_filtering=1 \
      --memtable_bloom_size_ratio=0 \
      --memtablerep=skip_list \
      --enable_speedb_features=0 \
      --use_spdb_writes=0 \
      --seed="$SEED" \
      --histogram=1 \
      --statistics=1 \
      --stats_level=3 \
      --perf_level=2 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readrandom de Speedb: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 5. ESTADÍSTICAS INTERNAS Y ESTRUCTURALES
  # ------------------------------------------------
  "$BIN" \
    --benchmarks=stats,levelstats,memstats,sstables \
    --use_existing_db=1 \
    --num="$NUM" \
    --cache_size="$CACHE_SIZE" \
    --write_buffer_size="$WRITE_BUFFER_SIZE" \
    --compression_type=snappy \
    --compression_ratio="$COMPRESSION_RATIO" \
    --bloom_bits="$BITS" \
    --use_ribbon_filter=0 \
    --whole_key_filtering=1 \
    --memtable_bloom_size_ratio=0 \
    --memtablerep=skip_list \
    --enable_speedb_features=0 \
    --use_spdb_writes=0 \
    --statistics=1 \
    --stats_level=3 \
    --db="$DB" \
    2>&1 | tee "${OUT}_db_stats.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error obteniendo estadísticas de Speedb: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 6. TAMAÑO REAL DEL DIRECTORIO
  # ------------------------------------------------
  du -sb "$DB" | tee "${OUT}_database_size.txt"

done
```

---

## 8. Verificar los archivos generados

```bash
find "$KVROOT/results/stage1" \
  -maxdepth 1 \
  -type f \
  -name "speedb_*" \
  | sort
```

Archivos esperados:

```text
speedb_nofilter_fillrandom.log
speedb_nofilter_fillrandom.time
speedb_nofilter_compact.log
speedb_nofilter_compact.time
speedb_nofilter_readmissing.log
speedb_nofilter_readmissing.time
speedb_nofilter_readrandom.log
speedb_nofilter_readrandom.time
speedb_nofilter_db_stats.log
speedb_nofilter_database_size.txt

speedb_bloom10_fillrandom.log
speedb_bloom10_fillrandom.time
speedb_bloom10_compact.log
speedb_bloom10_compact.time
speedb_bloom10_readmissing.log
speedb_bloom10_readmissing.time
speedb_bloom10_readrandom.log
speedb_bloom10_readrandom.time
speedb_bloom10_db_stats.log
speedb_bloom10_database_size.txt
```

---

## 9. Consultar resultados principales

Mostrar todas las cargas:

```bash
grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$KVROOT/results/stage1"/speedb_*.log
```

Mostrar solamente `readmissing`:

```bash
grep -HE \
  "^readmissing" \
  "$KVROOT/results/stage1"/speedb_*_readmissing.log
```

Mostrar solamente `readrandom`:

```bash
grep -HE \
  "^readrandom" \
  "$KVROOT/results/stage1"/speedb_*_readrandom.log
```

Mostrar el tamaño de las bases:

```bash
cat "$KVROOT/results/stage1"/speedb_*_database_size.txt
```

---

## 10. Consultar métricas de Bloom Filter

Buscar todas las métricas relacionadas con filtros:

```bash
grep -HEi \
  "bloom|filter" \
  "$KVROOT/results/stage1"/speedb_*.log
```

Buscar los contadores principales:

```bash
grep -HEi \
  "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
  "$KVROOT/results/stage1"/speedb_*.log
```

Buscar métricas de caché de filtros:

```bash
grep -HEi \
  "block.cache.filter" \
  "$KVROOT/results/stage1"/speedb_*.log
```

### Contadores relevantes

| Contador | Interpretación |
|---|---|
| `BLOOM_FILTER_USEFUL` | El filtro indicó que la clave no estaba y evitó revisar una SSTable. |
| `BLOOM_FILTER_FULL_POSITIVE` | El filtro respondió que la clave podría estar presente. |
| `BLOOM_FILTER_FULL_TRUE_POSITIVE` | El filtro respondió positivamente y la clave realmente estaba presente. |
| `BLOCK_CACHE_FILTER_HIT` | Bloque de filtro encontrado en caché. |
| `BLOCK_CACHE_FILTER_MISS` | Bloque de filtro no encontrado en caché. |
| `BLOCK_CACHE_FILTER_BYTES_INSERT` | Bytes de filtros insertados en la caché. |

En una carga `readmissing`, todas las claves solicitadas son inexistentes. Los positivos del filtro son, por tanto, candidatos a falsos positivos.

Una sola operación `Get` puede consultar filtros de varias SSTables. Estos contadores representan consultas internas a filtros, no necesariamente operaciones completas del benchmark.

---

## 11. Crear `speedb_summary.txt`

Este archivo almacena:

- Configuración utilizada.
- Versión y commit.
- Resultados principales.
- Contadores Bloom.
- Métricas de block cache.
- Estadísticas de lectura.
- Estadísticas de escritura y compactación.
- Tamaño de las bases.
- Estadísticas por niveles y SSTables.

```bash
SPEEDB_SUMMARY="$KVROOT/results/stage1/speedb_summary.txt"

{
  echo "============================================================"
  echo "SPEEDB 2.8.0 - BLOOM FILTER STAGE 1"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  echo "=== VERSIÓN ==="
  echo "Etiqueta: $(git -C "$KVROOT/engines/speedb-2.8.0" describe --tags --always)"
  echo "Commit: $(git -C "$KVROOT/engines/speedb-2.8.0" rev-parse HEAD)"
  echo

  echo "=== CONFIGURACIÓN ==="
  echo "NUM=$NUM"
  echo "READS=$READS"
  echo "VALUE_SIZE=$VALUE_SIZE"
  echo "THREADS=$THREADS"
  echo "CACHE_SIZE=$CACHE_SIZE bytes"
  echo "WRITE_BUFFER_SIZE=$WRITE_BUFFER_SIZE bytes"
  echo "COMPRESSION_TYPE=snappy"
  echo "COMPRESSION_RATIO=$COMPRESSION_RATIO"
  echo "MEMTABLE=skip_list"
  echo "ENABLE_SPEEDB_FEATURES=0"
  echo "USE_SPDB_WRITES=0"
  echo "MEMTABLE_BLOOM_SIZE_RATIO=0"
  echo "WHOLE_KEY_FILTERING=1"
  echo "USE_RIBBON_FILTER=0"
  echo "SEED=$SEED"
  echo "Sin filtro: bloom_bits=0"
  echo "Con Bloom: bloom_bits=$BLOOM_BITS"
  echo "HISTOGRAM=1"
  echo "STATISTICS=1"
  echo "STATS_LEVEL=3"
  echo "PERF_LEVEL=2"
  echo

  echo "=== RESULTADOS PRINCIPALES ==="

  grep -HE \
    "^fillrandom|^compact|^readmissing|^readrandom" \
    "$KVROOT/results/stage1"/speedb_*.log

  echo
  echo "=== CONTADORES BLOOM Y FILTROS ==="

  grep -HEi \
    "bloom.filter|block.cache.filter|filter.operation" \
    "$KVROOT/results/stage1"/speedb_*.log \
    || true

  echo
  echo "=== CONTADORES DE LECTURA Y CACHÉ ==="

  grep -HEi \
    "block.cache.data|block.cache.index|memtable.hit|memtable.miss|get.hit|number.keys.read|bytes.read" \
    "$KVROOT/results/stage1"/speedb_*.log \
    || true

  echo
  echo "=== CONTADORES DE ESCRITURA Y COMPACTACIÓN ==="

  grep -HEi \
    "number.keys.written|bytes.written|compact.read.bytes|compact.write.bytes|flush.write.bytes|stall" \
    "$KVROOT/results/stage1"/speedb_*.log \
    || true

  echo
  echo "=== TAMAÑO DE LAS BASES ==="

  for FILE in \
    "$KVROOT/results/stage1/speedb_nofilter_database_size.txt" \
    "$KVROOT/results/stage1/speedb_bloom10_database_size.txt"
  do
    if [ -f "$FILE" ]; then
      echo "--- $(basename "$FILE")"
      cat "$FILE"
    fi
  done

  echo
  echo "=== ESTADÍSTICAS ESTRUCTURALES ==="

  for FILE in \
    "$KVROOT/results/stage1/speedb_nofilter_db_stats.log" \
    "$KVROOT/results/stage1/speedb_bloom10_db_stats.log"
  do
    if [ -f "$FILE" ]; then
      echo
      echo "---------------- $(basename "$FILE") ----------------"
      cat "$FILE"
    fi
  done

} | tee "$SPEEDB_SUMMARY"
```

Abrir el resumen:

```bash
less "$SPEEDB_SUMMARY"
```

---

## 12. Crear `speedb_resources_summary.txt`

Este archivo extrae las métricas producidas por `/usr/bin/time -v`:

```bash
SPEEDB_RESOURCES="$KVROOT/results/stage1/speedb_resources_summary.txt"

{
  echo "============================================================"
  echo "SPEEDB 2.8.0 - RESOURCE METRICS"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  for FILE in "$KVROOT/results/stage1"/speedb_*.time; do

    [ -f "$FILE" ] || continue

    echo
    echo "------------------------------------------------------------"
    echo "ARCHIVO: $(basename "$FILE")"
    echo "------------------------------------------------------------"

    grep -E \
      "Command being timed|User time|System time|Percent of CPU|Elapsed \(wall clock\)|Maximum resident set size|Average resident set size|Major.*page faults|Minor.*page faults|Voluntary context switches|Involuntary context switches|File system inputs|File system outputs|Socket messages sent|Socket messages received|Exit status" \
      "$FILE"

  done

} | tee "$SPEEDB_RESOURCES"
```

Abrir:

```bash
less "$SPEEDB_RESOURCES"
```

---

## 13. Crear un informe combinado

```bash
SPEEDB_REPORT="$KVROOT/results/stage1/speedb_stage1_report.txt"

{
  cat "$KVROOT/results/stage1/speedb_summary.txt"

  echo
  echo
  echo "############################################################"
  echo "# MÉTRICAS DE RECURSOS"
  echo "############################################################"
  echo

  cat "$KVROOT/results/stage1/speedb_resources_summary.txt"

} > "$SPEEDB_REPORT"
```

Abrir:

```bash
less "$SPEEDB_REPORT"
```

---

## 14. Resumen rápido desde terminal

```bash
echo "=== SPEEDB: RENDIMIENTO ==="

grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$KVROOT/results/stage1"/speedb_*.log

echo
echo "=== SPEEDB: BLOOM FILTER ==="

grep -HEi \
  "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
  "$KVROOT/results/stage1"/speedb_*.log

echo
echo "=== SPEEDB: RAM MÁXIMA ==="

grep -H \
  "Maximum resident set size" \
  "$KVROOT/results/stage1"/speedb_*.time

echo
echo "=== SPEEDB: TAMAÑO DE BASES ==="

cat "$KVROOT/results/stage1"/speedb_*_database_size.txt
```

---

## 15. Interpretación general

| Métrica | Interpretación |
|---|---|
| `micros/op` | Latencia promedio por operación. Menor es mejor. |
| `ops/sec` | Operaciones completadas por segundo. Mayor es mejor. |
| `MB/s` | Throughput de datos. Mayor es mejor. |
| `BLOOM_FILTER_USEFUL` | Accesos a SSTables evitados por Bloom. |
| `BLOOM_FILTER_FULL_POSITIVE` | Respuestas positivas del filtro. |
| `BLOOM_FILTER_FULL_TRUE_POSITIVE` | Positivos en los que la clave realmente existía. |
| `BLOCK_CACHE_FILTER_HIT/MISS` | Comportamiento de los bloques de filtro en caché. |
| `Maximum resident set size` | Máxima RAM utilizada por el proceso. |
| `File system inputs/outputs` | Actividad reportada por el sistema de archivos. |
| `database_size` | Tamaño real de la base en disco. |

La comparación principal es:

```text
readmissing sin Bloom
           frente a
readmissing con Bloom de 10 bits
```

También deben analizarse:

- `fillrandom`, para medir el costo de construir y almacenar el filtro.
- `readrandom`, para observar el comportamiento con claves existentes.
- `compact`, para estudiar mantenimiento y reorganización de SSTables.
- Contadores Bloom, para explicar internamente los cambios de rendimiento.
- Tamaño de la base, para medir el costo espacial del filtro.

---

## 16. Comparación controlada frente a configuración nativa

Este documento utiliza una configuración controlada:

```text
memtablerep=skip_list
enable_speedb_features=0
use_spdb_writes=0
```

Su objetivo es aislar mejor el efecto de Bloom Filter y facilitar la comparación con RocksDB.

En un experimento posterior puede evaluarse la configuración nativa de Speedb:

```text
memtablerep=hash_spdb
optimizaciones específicas habilitadas
```

Ese segundo experimento respondería una pregunta diferente:

```text
¿Cuál es el rendimiento del motor completo utilizando sus optimizaciones propias?
```

No debe mezclarse directamente con la comparación controlada del filtro.

---

## 17. Archivos finales

```text
results/stage1/
├── speedb_summary.txt
├── speedb_resources_summary.txt
├── speedb_stage1_report.txt
├── speedb_nofilter_*.log
├── speedb_nofilter_*.time
├── speedb_bloom10_*.log
└── speedb_bloom10_*.time
```

---

## 18. Recomendaciones experimentales

- Ejecutar todas las pruebas en el mismo equipo.
- Mantener el equipo conectado a corriente.
- Evitar aplicaciones pesadas durante el benchmark.
- Utilizar la misma configuración en ambas condiciones.
- Ejecutar varias repeticiones antes de formular conclusiones.
- No comparar una ejecución instrumentada con otra sin instrumentación.
- Conservar los archivos `.log`, `.time` y los hashes de Git.
- Mantener `memtablerep=skip_list` para la comparación controlada.
- Mantener `use_ribbon_filter=0` durante la evaluación de Bloom tradicional.
- Mantener desactivadas las optimizaciones específicas de Speedb durante esta etapa.
- No modificar Speedb durante esta primera etapa.
