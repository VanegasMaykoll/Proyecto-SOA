# PebblesDB v1.0 — Evaluación de Bloom Filter con `db_bench`

Este documento describe el procedimiento utilizado para evaluar **PebblesDB v1.0** mediante su benchmark nativo `db_bench`, comparando dos condiciones:

- **Sin filtro Bloom**
- **Bloom Filter con 10 bits por clave**

Las pruebas ejecutadas son:

1. `fillrandom`: creación de la base mediante escrituras aleatorias.
2. `compact`: compactación completa.
3. `readmissing`: consultas de claves inexistentes.
4. `readrandom`: consultas aleatorias de claves existentes.
5. `stats,sstables`: estadísticas estructurales del motor.

También se almacenan:

- Salida completa de `db_bench`.
- Histogramas de latencia.
- Temporizadores internos de PebblesDB.
- Tiempo total y tiempo de CPU.
- Consumo máximo de memoria.
- Page faults.
- Cambios de contexto.
- Entradas y salidas del sistema de archivos.
- Tamaño final de cada base de datos.

---

## 1. Requisitos previos

Este procedimiento supone que PebblesDB v1.0 ya está descargado y compilado, y que existe el siguiente ejecutable:

```text
~/kv_engines_clean/bin/pebblesdb_db_bench
```

La estructura esperada del proyecto es:

```text
kv_engines_clean/
├── bin/
│   └── pebblesdb_db_bench
├── data/
├── engines/
│   └── pebblesdb-v1.0/
└── results/
```

Definir la ruta raíz:

```bash
export KVROOT="$HOME/kv_engines_clean"
```

Verificar el ejecutable:

```bash
ls -lh "$KVROOT/bin/pebblesdb_db_bench"
readlink -f "$KVROOT/bin/pebblesdb_db_bench"
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

## 2. Verificar la versión de PebblesDB

```bash
git -C "$KVROOT/engines/pebblesdb-v1.0" describe --tags --always
git -C "$KVROOT/engines/pebblesdb-v1.0" rev-parse HEAD
git -C "$KVROOT/engines/pebblesdb-v1.0" status
```

Versión utilizada:

```text
PebblesDB v1.0
Commit: 09c706d7aa2977c1316b2d64b81f1f1b6508002d
```

El ejecutable puede imprimir:

```text
LevelDB: version 1.17
```

Esto es normal. PebblesDB conserva internamente código y numeración heredados de LevelDB/HyperLevelDB. La versión válida para reproducibilidad es la etiqueta Git `v1.0` y su hash de commit.

---

## 3. Verificar que PebblesDB esté compilado en modo optimizado

PebblesDB debe compilarse con optimización y con las aserciones desactivadas:

```text
-O2 -DNDEBUG
```

Consultar las banderas almacenadas:

```bash
cd "$KVROOT/engines/pebblesdb-v1.0"

grep '^CXXFLAGS' Makefile
grep '^CFLAGS' Makefile
```

El resultado esperado debe contener:

```text
CXXFLAGS = -O2 -DNDEBUG
CFLAGS = -O2 -DNDEBUG
```

Si no aparecen estas opciones, recompilar:

```bash
cd "$KVROOT/engines/pebblesdb-v1.0"

make distclean 2>/dev/null || true
autoreconf -i

CFLAGS="-O2 -DNDEBUG" \
CXXFLAGS="-O2 -DNDEBUG" \
./configure

make -j"$(nproc)" db_bench
```

Actualizar el enlace simbólico:

```bash
ln -sfn \
  "$KVROOT/engines/pebblesdb-v1.0/db_bench" \
  "$KVROOT/bin/pebblesdb_db_bench"
```

---

## 4. Configuración experimental

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
  "COMPRESSION_RATIO=$COMPRESSION_RATIO"
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

En PebblesDB:

```text
--bloom_bits=-1   → filtro desactivado
--bloom_bits=10   → Bloom Filter con 10 bits por clave
```

---

## 5. Prueba rápida opcional

Antes de ejecutar el experimento completo:

```bash
rm -rf "$KVROOT/data/smoke_pebblesdb"

"$KVROOT/bin/pebblesdb_db_bench" \
  --benchmarks=fillseq \
  --num=10000 \
  --value_size=100 \
  --threads=1 \
  --db="$KVROOT/data/smoke_pebblesdb"
```

La salida esperada debe contener una línea similar a:

```text
fillseq : ... micros/op; ... MB/s
```

No debe aparecer:

```text
WARNING: Assertions are enabled; benchmarks unnecessarily slow
```

PebblesDB también puede mostrar secciones como:

```text
Timer information
Individual static timer information
Cumulative static timer information
```

Estas secciones son parte de la instrumentación interna del motor y no representan errores.

---

## 6. Ejecutar el experimento completo

Este bloque ejecuta ambas condiciones:

- `nofilter`: sin Bloom Filter.
- `bloom10`: Bloom Filter con 10 bits por clave.

Cada prueba genera:

- Un archivo `.log` con la salida completa de `db_bench`.
- Un archivo `.time` con las métricas de `/usr/bin/time -v`.

```bash
BIN="$KVROOT/bin/pebblesdb_db_bench"

for MODE in nofilter bloom10; do

  if [ "$MODE" = "nofilter" ]; then
    BITS=-1
  else
    BITS="$BLOOM_BITS"
  fi

  DB="$KVROOT/data/stage1/pebblesdb_${MODE}"
  OUT="$KVROOT/results/stage1/pebblesdb_${MODE}"

  echo
  echo "=================================================="
  echo "PebblesDB v1.0"
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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_fillrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante fillrandom de PebblesDB: $MODE"
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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --db="$DB" \
    2>&1 | tee "${OUT}_compact.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante compact de PebblesDB: $MODE"
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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readmissing.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readmissing de PebblesDB: $MODE"
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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$BITS" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readrandom.log"

  if [ "${PIPESTATUS[0]}" -ne 0 ]; then
    echo "Error durante readrandom de PebblesDB: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 5. ESTADÍSTICAS Y SSTABLES
  # ------------------------------------------------
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
    echo "Error obteniendo estadísticas de PebblesDB: $MODE"
    exit 1
  fi

  # ------------------------------------------------
  # 6. TAMAÑO REAL DEL DIRECTORIO
  # ------------------------------------------------
  du -sb "$DB" | tee "${OUT}_database_size.txt"

done
```

---

## 7. Verificar los archivos generados

```bash
find "$KVROOT/results/stage1" \
  -maxdepth 1 \
  -type f \
  -name "pebblesdb_*" \
  | sort
```

Archivos esperados:

```text
pebblesdb_nofilter_fillrandom.log
pebblesdb_nofilter_fillrandom.time
pebblesdb_nofilter_compact.log
pebblesdb_nofilter_compact.time
pebblesdb_nofilter_readmissing.log
pebblesdb_nofilter_readmissing.time
pebblesdb_nofilter_readrandom.log
pebblesdb_nofilter_readrandom.time
pebblesdb_nofilter_db_stats.log
pebblesdb_nofilter_database_size.txt

pebblesdb_bloom10_fillrandom.log
pebblesdb_bloom10_fillrandom.time
pebblesdb_bloom10_compact.log
pebblesdb_bloom10_compact.time
pebblesdb_bloom10_readmissing.log
pebblesdb_bloom10_readmissing.time
pebblesdb_bloom10_readrandom.log
pebblesdb_bloom10_readrandom.time
pebblesdb_bloom10_db_stats.log
pebblesdb_bloom10_database_size.txt
```

---

## 8. Consultar resultados principales

Mostrar las cargas principales:

```bash
grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$KVROOT/results/stage1"/pebblesdb_*.log
```

Mostrar solamente `readmissing`:

```bash
grep -HE \
  "^readmissing" \
  "$KVROOT/results/stage1"/pebblesdb_*_readmissing.log
```

Mostrar solamente `readrandom`:

```bash
grep -HE \
  "^readrandom" \
  "$KVROOT/results/stage1"/pebblesdb_*_readrandom.log
```

Mostrar referencias a los temporizadores internos:

```bash
grep -HE \
  "Timer information|Individual static timer|Cumulative static timer" \
  "$KVROOT/results/stage1"/pebblesdb_*.log
```

Mostrar el tamaño de las bases:

```bash
cat "$KVROOT/results/stage1"/pebblesdb_*_database_size.txt
```

---

## 9. Crear `pebblesdb_summary.txt`

Este archivo almacena:

- Configuración utilizada.
- Versión y commit.
- Resultados de `fillrandom`.
- Resultados de `compact`.
- Resultados de `readmissing`.
- Resultados de `readrandom`.
- Tamaño de las bases.
- Estadísticas internas.
- Distribución de SSTables.

```bash
PEBBLES_SUMMARY="$KVROOT/results/stage1/pebblesdb_summary.txt"

{
  echo "============================================================"
  echo "PEBBLESDB v1.0 - BLOOM FILTER STAGE 1"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  echo "=== VERSIÓN ==="
  echo "Etiqueta: $(git -C "$KVROOT/engines/pebblesdb-v1.0" describe --tags --always)"
  echo "Commit: $(git -C "$KVROOT/engines/pebblesdb-v1.0" rev-parse HEAD)"
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
  echo

  echo "=== RESULTADOS PRINCIPALES ==="

  grep -HE \
    "^fillrandom|^compact|^readmissing|^readrandom" \
    "$KVROOT/results/stage1"/pebblesdb_*.log

  echo
  echo "=== TAMAÑO DE LAS BASES ==="

  for FILE in \
    "$KVROOT/results/stage1/pebblesdb_nofilter_database_size.txt" \
    "$KVROOT/results/stage1/pebblesdb_bloom10_database_size.txt"
  do
    if [ -f "$FILE" ]; then
      echo "--- $(basename "$FILE")"
      cat "$FILE"
    fi
  done

  echo
  echo "=== ESTADÍSTICAS ESTRUCTURALES ==="

  for FILE in \
    "$KVROOT/results/stage1/pebblesdb_nofilter_db_stats.log" \
    "$KVROOT/results/stage1/pebblesdb_bloom10_db_stats.log"
  do
    if [ -f "$FILE" ]; then
      echo
      echo "---------------- $(basename "$FILE") ----------------"
      cat "$FILE"
    fi
  done

  echo
  echo "=== TEMPORIZADORES INTERNOS ==="

  grep -HE \
    "Timer information|Individual static timer|Cumulative static timer" \
    "$KVROOT/results/stage1"/pebblesdb_*.log \
    || true

} | tee "$PEBBLES_SUMMARY"
```

Abrir el resumen:

```bash
less "$PEBBLES_SUMMARY"
```

---

## 10. Crear `pebblesdb_resources_summary.txt`

Este archivo extrae las métricas de `/usr/bin/time -v`:

- Tiempo de CPU de usuario.
- Tiempo de CPU del sistema.
- Porcentaje de CPU.
- Tiempo total.
- Memoria máxima.
- Page faults.
- Cambios de contexto.
- Entradas y salidas del sistema de archivos.
- Estado de salida.

```bash
PEBBLES_RESOURCES="$KVROOT/results/stage1/pebblesdb_resources_summary.txt"

{
  echo "============================================================"
  echo "PEBBLESDB v1.0 - RESOURCE METRICS"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  for FILE in "$KVROOT/results/stage1"/pebblesdb_*.time; do

    [ -f "$FILE" ] || continue

    echo
    echo "------------------------------------------------------------"
    echo "ARCHIVO: $(basename "$FILE")"
    echo "------------------------------------------------------------"

    grep -E \
      "Command being timed|User time|System time|Percent of CPU|Elapsed \(wall clock\)|Maximum resident set size|Average resident set size|Major.*page faults|Minor.*page faults|Voluntary context switches|Involuntary context switches|File system inputs|File system outputs|Socket messages sent|Socket messages received|Exit status" \
      "$FILE"

  done

} | tee "$PEBBLES_RESOURCES"
```

Abrir el resumen:

```bash
less "$PEBBLES_RESOURCES"
```

---

## 11. Crear un informe combinado

```bash
PEBBLES_REPORT="$KVROOT/results/stage1/pebblesdb_stage1_report.txt"

{
  cat "$KVROOT/results/stage1/pebblesdb_summary.txt"

  echo
  echo
  echo "############################################################"
  echo "# MÉTRICAS DE RECURSOS"
  echo "############################################################"
  echo

  cat "$KVROOT/results/stage1/pebblesdb_resources_summary.txt"

} > "$PEBBLES_REPORT"
```

Abrir el informe:

```bash
less "$PEBBLES_REPORT"
```

---

## 12. Resumen rápido desde terminal

```bash
echo "=== PEBBLESDB: RENDIMIENTO ==="

grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$KVROOT/results/stage1"/pebblesdb_*.log

echo
echo "=== PEBBLESDB: RAM MÁXIMA ==="

grep -H \
  "Maximum resident set size" \
  "$KVROOT/results/stage1"/pebblesdb_*.time

echo
echo "=== PEBBLESDB: TAMAÑO DE BASES ==="

cat "$KVROOT/results/stage1"/pebblesdb_*_database_size.txt
```

---

## 13. Interpretación general

| Métrica | Interpretación |
|---|---|
| `micros/op` | Latencia promedio por operación. Menor es mejor. |
| `MB/s` | Throughput de datos. Mayor es mejor. |
| Temporizadores internos | Instrumentación propia de PebblesDB. |
| `Maximum resident set size` | Máxima memoria RAM utilizada. |
| `File system inputs` | Entradas realizadas por el sistema de archivos. |
| `File system outputs` | Salidas realizadas por el sistema de archivos. |
| `database_size` | Tamaño real de la base en disco. |

La comparación principal para Bloom Filter es:

```text
readmissing sin Bloom
           frente a
readmissing con Bloom de 10 bits
```

El resultado esperado es que Bloom Filter reduzca el costo de las búsquedas inexistentes al evitar accesos innecesarios a SSTables.

También deben analizarse:

- `fillrandom`, para estudiar el costo de crear y almacenar el filtro.
- `readrandom`, para observar búsquedas exitosas.
- `compact`, para analizar diferencias estructurales y de mantenimiento.
- Los temporizadores internos de PebblesDB, como información complementaria.

PebblesDB no ofrece de forma nativa los mismos contadores específicos de Bloom que RocksDB y Speedb. Durante esta etapa no se modifica el código del motor.

---

## 14. Archivos finales

```text
results/stage1/
├── pebblesdb_summary.txt
├── pebblesdb_resources_summary.txt
├── pebblesdb_stage1_report.txt
├── pebblesdb_nofilter_*.log
├── pebblesdb_nofilter_*.time
├── pebblesdb_bloom10_*.log
└── pebblesdb_bloom10_*.time
```

---

## 15. Recomendaciones experimentales

- Ejecutar las pruebas en el mismo equipo.
- Mantener el equipo conectado a corriente.
- Evitar aplicaciones pesadas durante el benchmark.
- Usar las mismas variables para todos los motores.
- Ejecutar varias repeticiones antes de formular conclusiones.
- No comparar una ejecución instrumentada con otra sin instrumentación.
- Conservar los archivos `.log`, `.time` y los hashes de Git.
- Verificar que PebblesDB esté compilado con `-O2 -DNDEBUG`.
- No modificar PebblesDB durante esta primera etapa.
