# Speedb 2.8.0 — Experimento B: Bloom Filter vs Ribbon Filter

Este documento describe el procedimiento utilizado en la **fase B del proyecto** para comparar, dentro de **Speedb 2.8.0**, dos estructuras probabilísticas de pertenencia asociadas a SSTables:

- **Bloom Filter**
- **Ribbon Filter**

La comparación se diseñó de forma controlada, manteniendo constantes la carga, la representación de la memtable, el tamaño de caché, la compresión, la semilla, la instrumentación y el resto de parámetros del motor.

La única variable experimental modificada fue:

```text
Tipo de filtro probabilístico:
Bloom Filter
frente a
Ribbon Filter
```

---

## 1. Objetivo del experimento

El objetivo fue evaluar cómo cambia el comportamiento de Speedb al sustituir Bloom Filter por Ribbon Filter bajo una carga idéntica.

Las dimensiones analizadas fueron:

- Costo de construcción del filtro.
- Tiempo de compactación.
- Latencia de consultas inexistentes.
- Latencia de consultas exitosas.
- Throughput.
- Tasa observada de falsos positivos.
- Uso de CPU.
- Uso máximo de memoria.
- Actividad del sistema de archivos.
- Tamaño final de la base.
- Estadísticas internas del filtro y block cache.

---

## 2. Pregunta experimental

La pregunta principal es:

> ¿Cómo cambia el rendimiento, el consumo de recursos y el tamaño de almacenamiento de Speedb 2.8.0 cuando se utiliza Ribbon Filter en lugar de Bloom Filter, manteniendo una precisión equivalente y el resto de la configuración constante?

---

## 3. Hipótesis general

Ribbon Filter está diseñado para utilizar menos espacio permanente que Bloom Filter manteniendo una tasa de falsos positivos aproximadamente equivalente.

Sin embargo, Ribbon puede requerir:

- Más CPU durante la construcción.
- Más memoria temporal.
- Mayor costo durante flush o compactación.
- Un tiempo de creación superior al de Bloom.

Por ello, no se espera necesariamente que Ribbon sea más rápido en todas las operaciones.

La ventaja principal esperada es:

```text
Precisión similar
+
menor espacio permanente del filtro
```

a cambio de:

```text
Mayor costo de construcción
```

---

## 4. Versión utilizada

Motor:

```text
Speedb 2.8.0
```

Etiqueta Git:

```text
speedb/v2.8.0
```

Commit:

```text
c328f0ebc33db0166cd2a2844ff44c8034e30e5b
```

Verificación:

```bash
export KVROOT="$HOME/kv_engines_clean"

git -C "$KVROOT/engines/speedb-2.8.0" describe --tags --always
git -C "$KVROOT/engines/speedb-2.8.0" rev-parse HEAD
git -C "$KVROOT/engines/speedb-2.8.0" status
```

---

## 5. Estructura esperada del proyecto

```text
kv_engines_clean/
├── bin/
│   └── speedb_db_bench
├── data/
│   └── experiment_b/
│       └── speedb/
├── engines/
│   └── speedb-2.8.0/
└── results/
    └── experiment_b/
        └── speedb/
```

El ejecutable utilizado es:

```text
~/kv_engines_clean/bin/speedb_db_bench
```

Verificación:

```bash
ls -lh "$KVROOT/bin/speedb_db_bench"
readlink -f "$KVROOT/bin/speedb_db_bench"
file "$KVROOT/bin/speedb_db_bench"
```

---

## 6. Requisito de compilación

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

## 7. Distinción entre memtable y filtro probabilístico

En este experimento se mantuvo una **SkipList** como representación de la memtable:

```bash
--memtablerep=skip_list
```

SkipList no es el filtro probabilístico evaluado.

Las dos capas son diferentes:

```text
Memtable:
organiza datos reales en RAM
→ SkipList

Filtro de SSTables:
responde pertenencia aproximada
→ Bloom Filter o Ribbon Filter
```

Por tanto, el experimento modificó únicamente el filtro de SSTables.

---

## 8. Razón para utilizar SkipList

Speedb utiliza normalmente su propia representación:

```text
hash_spdb
```

Para evitar que el experimento comparara simultáneamente:

- Diferentes filtros.
- Diferentes memtables.
- Diferentes optimizaciones del motor.

se utilizó:

```bash
--memtablerep=skip_list
```

Así, la comparación fue:

```text
Speedb + SkipList + Bloom
frente a
Speedb + SkipList + Ribbon
```

---

## 9. Desactivación de funciones adicionales de Speedb

También se desactivaron opciones propias que podrían alterar el comportamiento:

```bash
--enable_speedb_features=0
--use_spdb_writes=0
```

Interpretación:

| Opción | Propósito |
|---|---|
| `enable_speedb_features=0` | Evita activar automáticamente optimizaciones adicionales. |
| `use_spdb_writes=0` | Desactiva el flujo de escrituras específico de Speedb. |

Esta decisión permite aislar mejor el efecto del filtro probabilístico.

> El experimento no busca medir el rendimiento máximo de Speedb con todas sus funciones nativas activadas. Busca comparar Bloom y Ribbon bajo una configuración controlada.

---

## 10. Configuración de los filtros

### Condición Bloom

```bash
--bloom_bits=10
--use_ribbon_filter=0
```

### Condición Ribbon

```bash
--bloom_bits=10
--use_ribbon_filter=1
```

En el caso de Ribbon, el valor `10` no representa necesariamente 10 bits físicos por clave.

Representa una precisión objetivo aproximadamente equivalente a un Bloom Filter de 10 bits por clave.

Por ello, los nombres utilizados fueron:

```text
bloom10
ribbon10eq
```

donde:

```text
ribbon10eq
=
Ribbon con precisión equivalente a Bloom 10
```

---

## 11. Consideración sobre la política Ribbon

La política Ribbon utilizada por `db_bench` puede emplear:

- Bloom en algunos archivos producidos directamente por flush.
- Ribbon en archivos producidos posteriormente por compactación.

Por este motivo, el flujo experimental incluye una compactación completa antes de las lecturas:

```text
fillrandom
→ compact
→ readmissing
→ readrandom
```

Esto favorece que las SSTables evaluadas durante las lecturas hayan sido reconstruidas bajo la política Ribbon.

La denominación técnica más precisa es:

```text
Política Ribbon de Speedb
con precisión equivalente a Bloom 10
```

---

## 12. Configuración experimental común

Ejecutar:

```bash
export KVROOT="$HOME/kv_engines_clean"

export NUM=1000000
export READS=1000000
export VALUE_SIZE=1024
export THREADS=1

export CACHE_SIZE=$((32 * 1024 * 1024))
export WRITE_BUFFER_SIZE=$((4 * 1024 * 1024))

export FILTER_BITS=10
export COMPRESSION_RATIO=1.0
export SEED=20260724

export SPEEDB_DATA="$KVROOT/data/experiment_b/speedb"
export SPEEDB_RESULTS="$KVROOT/results/experiment_b/speedb"

mkdir -p "$SPEEDB_DATA"
mkdir -p "$SPEEDB_RESULTS"

set -o pipefail
```

Comprobar:

```bash
printf '%s\n' \
  "KVROOT=$KVROOT" \
  "NUM=$NUM" \
  "READS=$READS" \
  "VALUE_SIZE=$VALUE_SIZE" \
  "THREADS=$THREADS" \
  "CACHE_SIZE=$CACHE_SIZE" \
  "WRITE_BUFFER_SIZE=$WRITE_BUFFER_SIZE" \
  "FILTER_BITS=$FILTER_BITS" \
  "COMPRESSION_RATIO=$COMPRESSION_RATIO" \
  "SEED=$SEED" \
  "SPEEDB_DATA=$SPEEDB_DATA" \
  "SPEEDB_RESULTS=$SPEEDB_RESULTS"
```

### Resumen de parámetros

| Parámetro | Valor |
|---|---:|
| Registros | 1 000 000 |
| Lecturas | 1 000 000 |
| Tamaño del valor | 1024 bytes |
| Hilos | 1 |
| Block cache | 32 MiB |
| Write buffer | 4 MiB |
| Filtro | Bloom o Ribbon |
| Equivalencia | Bloom 10 bits por clave |
| Memtable | SkipList |
| Compresión | Snappy |
| Compression ratio | 1.0 |
| Semilla | 20260724 |

---

## 13. Verificar los flags del binario

```bash
BIN="$KVROOT/bin/speedb_db_bench"

"$BIN" --help 2>&1 \
  | grep -Ei \
    "bloom_bits|use_ribbon_filter|whole_key_filtering|memtablerep|enable_speedb_features|use_spdb_writes"
```

Deben aparecer opciones relacionadas con:

```text
bloom_bits
use_ribbon_filter
whole_key_filtering
memtablerep
enable_speedb_features
use_spdb_writes
```

---

## 14. Instrumentación habilitada

Se utilizaron las siguientes opciones:

```bash
--histogram=1
--statistics=1
--stats_level=3
--perf_level=2
```

| Opción | Propósito |
|---|---|
| `histogram=1` | Imprime distribución de latencias. |
| `statistics=1` | Activa tickers e histogramas internos. |
| `stats_level=3` | Activa estadísticas detalladas. |
| `perf_level=2` | Activa contadores de `PerfContext`. |

La instrumentación se mantuvo idéntica en ambas condiciones.

---

## 15. Flujo del experimento

Para cada filtro se ejecutó:

```text
1. Eliminar la base anterior.
2. Crear la base con fillrandom.
3. Ejecutar compactación completa.
4. Ejecutar readmissing.
5. Ejecutar readrandom.
6. Obtener stats, levelstats, memstats y sstables.
7. Medir tamaño del directorio.
8. Guardar logs y métricas de recursos.
```

---

## 16. Ejecutar Bloom y Ribbon

```bash
BIN="$KVROOT/bin/speedb_db_bench"

for FILTER in bloom10 ribbon10eq; do

  if [ "$FILTER" = "bloom10" ]; then
    USE_RIBBON=0
    FILTER_NAME="Bloom Filter"
  else
    USE_RIBBON=1
    FILTER_NAME="Ribbon Filter, Bloom-10 equivalent"
  fi

  DB="$SPEEDB_DATA/speedb_${FILTER}"
  OUT="$SPEEDB_RESULTS/speedb_${FILTER}"

  echo
  echo "============================================================"
  echo "Speedb 2.8.0 — Experimento B"
  echo "Filtro: $FILTER_NAME"
  echo "bloom_bits=$FILTER_BITS"
  echo "use_ribbon_filter=$USE_RIBBON"
  echo "Base: $DB"
  echo "============================================================"

  rm -rf "$DB"

  # ----------------------------------------------------------
  # 1. FILLRANDOM
  # ----------------------------------------------------------
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
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
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
    echo "ERROR: fillrandom falló para $FILTER"
    exit 1
  fi

  # ----------------------------------------------------------
  # 2. COMPACT
  # ----------------------------------------------------------
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
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
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
    echo "ERROR: compact falló para $FILTER"
    exit 1
  fi

  # ----------------------------------------------------------
  # 3. READMISSING
  # ----------------------------------------------------------
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
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
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
    echo "ERROR: readmissing falló para $FILTER"
    exit 1
  fi

  # ----------------------------------------------------------
  # 4. READRANDOM
  # ----------------------------------------------------------
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
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
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
    echo "ERROR: readrandom falló para $FILTER"
    exit 1
  fi

  # ----------------------------------------------------------
  # 5. ESTADÍSTICAS
  # ----------------------------------------------------------
  "$BIN" \
    --benchmarks=stats,levelstats,memstats,sstables \
    --use_existing_db=1 \
    --num="$NUM" \
    --cache_size="$CACHE_SIZE" \
    --write_buffer_size="$WRITE_BUFFER_SIZE" \
    --compression_type=snappy \
    --compression_ratio="$COMPRESSION_RATIO" \
    --bloom_bits="$FILTER_BITS" \
    --use_ribbon_filter="$USE_RIBBON" \
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
    echo "ERROR: las estadísticas fallaron para $FILTER"
    exit 1
  fi

  # ----------------------------------------------------------
  # 6. TAMAÑO DE LA BASE
  # ----------------------------------------------------------
  du -sb "$DB" \
    | tee "${OUT}_database_size.txt"

done
```

---

## 17. Archivos generados

```bash
find "$SPEEDB_RESULTS" \
  -maxdepth 1 \
  -type f \
  | sort
```

Archivos esperados:

```text
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

speedb_ribbon10eq_fillrandom.log
speedb_ribbon10eq_fillrandom.time
speedb_ribbon10eq_compact.log
speedb_ribbon10eq_compact.time
speedb_ribbon10eq_readmissing.log
speedb_ribbon10eq_readmissing.time
speedb_ribbon10eq_readrandom.log
speedb_ribbon10eq_readrandom.time
speedb_ribbon10eq_db_stats.log
speedb_ribbon10eq_database_size.txt
```

---

## 18. Consultar resultados principales

```bash
grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$SPEEDB_RESULTS"/speedb_*.log
```

Solo `readmissing`:

```bash
grep -HE \
  "^readmissing" \
  "$SPEEDB_RESULTS"/speedb_*_readmissing.log
```

Solo `readrandom`:

```bash
grep -HE \
  "^readrandom" \
  "$SPEEDB_RESULTS"/speedb_*_readrandom.log
```

Construcción:

```bash
grep -HE \
  "^fillrandom|^compact" \
  "$SPEEDB_RESULTS"/speedb_*.log
```

Tamaño de las bases:

```bash
cat "$SPEEDB_RESULTS"/speedb_*_database_size.txt
```

---

## 19. Contadores del filtro

Aunque se utilice Ribbon, los contadores internos conservan nombres históricos asociados a Bloom:

```text
rocksdb.bloom.filter.useful
rocksdb.bloom.filter.full.positive
rocksdb.bloom.filter.full.true.positive
```

Buscar:

```bash
grep -HEi \
  "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
  "$SPEEDB_RESULTS"/speedb_*.log
```

Solo en `readmissing`:

```bash
grep -HEi \
  "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
  "$SPEEDB_RESULTS"/speedb_*_readmissing.log
```

Métricas de caché:

```bash
grep -HEi \
  "block.cache.filter" \
  "$SPEEDB_RESULTS"/speedb_*.log
```

Tiempo relacionado con filtros:

```bash
grep -HEi \
  "filter.operation|filter.*time" \
  "$SPEEDB_RESULTS"/speedb_*.log
```

---

## 20. Interpretación de contadores

| Contador | Significado |
|---|---|
| `BLOOM_FILTER_USEFUL` | Negativos correctos que evitaron revisar una SSTable. |
| `BLOOM_FILTER_FULL_POSITIVE` | Respuestas positivas del filtro. |
| `BLOOM_FILTER_FULL_TRUE_POSITIVE` | Positivos en los que la clave realmente existía. |

En `readmissing`, todas las claves son inexistentes.

Por tanto:

```text
Falsos positivos estimados
=
FULL_POSITIVE
-
FULL_TRUE_POSITIVE
```

Una operación `Get` puede consultar más de una SSTable.

Por ello, estos contadores representan consultas internas al filtro y no necesariamente el mismo número de operaciones ejecutadas por `db_bench`.

---

## 21. Crear `speedb_bloom_vs_ribbon_summary.txt`

```bash
SPEEDB_B_R_SUMMARY="$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_summary.txt"

{
  echo "============================================================"
  echo "SPEEDB 2.8.0 — EXPERIMENTO B"
  echo "BLOOM FILTER VS RIBBON FILTER"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  echo "=== VERSIÓN ==="
  echo "Etiqueta: $(git -C "$KVROOT/engines/speedb-2.8.0" describe --tags --always)"
  echo "Commit: $(git -C "$KVROOT/engines/speedb-2.8.0" rev-parse HEAD)"
  echo

  echo "=== CONFIGURACIÓN COMÚN ==="
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
  echo "SEED=$SEED"
  echo "HISTOGRAM=1"
  echo "STATISTICS=1"
  echo "STATS_LEVEL=3"
  echo "PERF_LEVEL=2"
  echo

  echo "=== CONDICIONES ==="
  echo "Bloom:"
  echo "  bloom_bits=$FILTER_BITS"
  echo "  use_ribbon_filter=0"
  echo
  echo "Ribbon:"
  echo "  bloom_equivalent_bits=$FILTER_BITS"
  echo "  use_ribbon_filter=1"
  echo

  echo "=== RESULTADOS PRINCIPALES ==="

  grep -HE \
    "^fillrandom|^compact|^readmissing|^readrandom" \
    "$SPEEDB_RESULTS"/speedb_*.log

  echo
  echo "=== CONTADORES DE FILTROS ==="

  grep -HEi \
    "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
    "$SPEEDB_RESULTS"/speedb_*.log \
    || true

  echo
  echo "=== MÉTRICAS DE CACHÉ DE FILTROS ==="

  grep -HEi \
    "block.cache.filter" \
    "$SPEEDB_RESULTS"/speedb_*.log \
    || true

  echo
  echo "=== CONTADORES DE LECTURA Y CACHÉ ==="

  grep -HEi \
    "block.cache.data|block.cache.index|memtable.hit|memtable.miss|get.hit|number.keys.read|bytes.read" \
    "$SPEEDB_RESULTS"/speedb_*.log \
    || true

  echo
  echo "=== ESCRITURA Y COMPACTACIÓN ==="

  grep -HEi \
    "number.keys.written|bytes.written|compact.read.bytes|compact.write.bytes|flush.write.bytes|filter.operation.total.time|stall" \
    "$SPEEDB_RESULTS"/speedb_*.log \
    || true

  echo
  echo "=== TAMAÑO DE LAS BASES ==="

  for FILE in \
    "$SPEEDB_RESULTS/speedb_bloom10_database_size.txt" \
    "$SPEEDB_RESULTS/speedb_ribbon10eq_database_size.txt"
  do
    if [ -f "$FILE" ]; then
      echo "--- $(basename "$FILE")"
      cat "$FILE"
    fi
  done

  echo
  echo "=== ESTADÍSTICAS ESTRUCTURALES ==="

  for FILE in \
    "$SPEEDB_RESULTS/speedb_bloom10_db_stats.log" \
    "$SPEEDB_RESULTS/speedb_ribbon10eq_db_stats.log"
  do
    if [ -f "$FILE" ]; then
      echo
      echo "---------------- $(basename "$FILE") ----------------"
      cat "$FILE"
    fi
  done

} | tee "$SPEEDB_B_R_SUMMARY"
```

---

## 22. Crear `speedb_bloom_vs_ribbon_resources_summary.txt`

```bash
SPEEDB_B_R_RESOURCES="$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_resources_summary.txt"

{
  echo "============================================================"
  echo "SPEEDB 2.8.0 — BLOOM VS RIBBON"
  echo "RESOURCE METRICS"
  echo "Fecha: $(date --iso-8601=seconds)"
  echo "============================================================"
  echo

  for FILE in "$SPEEDB_RESULTS"/speedb_*.time; do

    [ -f "$FILE" ] || continue

    echo
    echo "------------------------------------------------------------"
    echo "ARCHIVO: $(basename "$FILE")"
    echo "------------------------------------------------------------"

    grep -E \
      "Command being timed|User time|System time|Percent of CPU|Elapsed \(wall clock\)|Maximum resident set size|Average resident set size|Major.*page faults|Minor.*page faults|Voluntary context switches|Involuntary context switches|File system inputs|File system outputs|Socket messages sent|Socket messages received|Exit status" \
      "$FILE"

  done

} | tee "$SPEEDB_B_R_RESOURCES"
```

---

## 23. Calcular la tasa observada de falsos positivos

Para `readmissing`:

```text
Negativos útiles
=
BLOOM_FILTER_USEFUL
```

```text
Falsos positivos estimados
=
BLOOM_FILTER_FULL_POSITIVE
-
BLOOM_FILTER_FULL_TRUE_POSITIVE
```

```text
FPR observado
=
falsos positivos
/
(falsos positivos + negativos útiles)
```

Ejecutar:

```bash
python3 - <<'PY' \
  | tee "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_fpr_summary.txt"

from pathlib import Path
import os
import re

results = Path(os.environ["SPEEDB_RESULTS"])

def extract_counter(text: str, suffix: str) -> int:
    patterns = [
        rf"(?:rocksdb\.)?{re.escape(suffix)}\s+COUNT\s*:\s*([0-9]+)",
        rf"(?:rocksdb\.)?{re.escape(suffix)}[^0-9]+([0-9]+)",
    ]

    for pattern in patterns:
        values = re.findall(pattern, text, flags=re.IGNORECASE)
        if values:
            return int(values[-1])

    return 0

print("=" * 68)
print("SPEEDB 2.8.0 — OBSERVED FILTER FALSE-POSITIVE RATE")
print("=" * 68)

for mode in ("bloom10", "ribbon10eq"):
    path = results / f"speedb_{mode}_readmissing.log"
    text = path.read_text(encoding="utf-8", errors="ignore")

    useful = extract_counter(
        text,
        "bloom.filter.useful",
    )

    positive = extract_counter(
        text,
        "bloom.filter.full.positive",
    )

    true_positive = extract_counter(
        text,
        "bloom.filter.full.true.positive",
    )

    false_positive = max(
        0,
        positive - true_positive,
    )

    negative_probes = useful + false_positive

    if negative_probes > 0:
        fpr = false_positive / negative_probes
        fpr_percent = fpr * 100.0
        formatted_fpr = f"{fpr_percent:.6f}%"
    else:
        formatted_fpr = "N/A — counters not found or zero"

    print()
    print(f"Condition: {mode}")
    print(f"  Useful negatives:       {useful}")
    print(f"  Full positives:         {positive}")
    print(f"  Full true positives:    {true_positive}")
    print(f"  Estimated false pos.:   {false_positive}")
    print(f"  Negative filter probes: {negative_probes}")
    print(f"  Observed FPR:           {formatted_fpr}")
PY
```

Abrir:

```bash
cat "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_fpr_summary.txt"
```

---

## 24. Crear el informe combinado

```bash
SPEEDB_B_R_REPORT="$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_report.txt"

{
  cat "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_summary.txt"

  echo
  echo
  echo "############################################################"
  echo "# RESOURCE METRICS"
  echo "############################################################"
  echo

  cat "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_resources_summary.txt"

  echo
  echo
  echo "############################################################"
  echo "# OBSERVED FALSE-POSITIVE RATE"
  echo "############################################################"
  echo

  cat "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_fpr_summary.txt"

} > "$SPEEDB_B_R_REPORT"
```

Abrir:

```bash
less "$SPEEDB_B_R_REPORT"
```

---

## 25. Resumen rápido

```bash
echo "=== SPEEDB: RENDIMIENTO ==="

grep -HE \
  "^fillrandom|^compact|^readmissing|^readrandom" \
  "$SPEEDB_RESULTS"/speedb_*.log

echo
echo "=== SPEEDB: CONTADORES DE FILTRO ==="

grep -HEi \
  "bloom.filter.useful|bloom.filter.full.positive|bloom.filter.full.true.positive" \
  "$SPEEDB_RESULTS"/speedb_*.log

echo
echo "=== SPEEDB: RAM MÁXIMA ==="

grep -H \
  "Maximum resident set size" \
  "$SPEEDB_RESULTS"/speedb_*.time

echo
echo "=== SPEEDB: TAMAÑO DE BASES ==="

cat "$SPEEDB_RESULTS"/speedb_*_database_size.txt

echo
echo "=== SPEEDB: FPR OBSERVADO ==="

cat "$SPEEDB_RESULTS/speedb_bloom_vs_ribbon_fpr_summary.txt"
```

---

## 26. Métricas principales para el análisis

### Construcción

- `fillrandom micros/op`
- `fillrandom ops/sec`
- Tiempo de `compact`
- Tiempo de CPU de usuario
- RAM máxima
- Tiempo asociado a operaciones del filtro

### Consultas inexistentes

- `readmissing micros/op`
- `readmissing ops/sec`
- Percentiles de latencia
- `BLOOM_FILTER_USEFUL`
- `FULL_POSITIVE`
- FPR observado

### Consultas exitosas

- `readrandom micros/op`
- `readrandom ops/sec`
- Percentiles de latencia
- `FULL_TRUE_POSITIVE`

### Espacio

- Tamaño total de la base
- Bytes de filtros en block cache
- Tamaño de SSTables
- RAM máxima

---

## 27. Interpretación esperada

Bloom puede mostrar:

- Menor costo de construcción.
- Menor consumo temporal de CPU y memoria.
- Mayor espacio permanente.

Ribbon puede mostrar:

- Mayor costo durante construcción o compactación.
- Menor espacio utilizado por el filtro.
- Precisión similar a Bloom 10.
- Rendimiento de lectura similar, mejor o ligeramente peor dependiendo de la carga.

Por tanto, la evaluación no debe limitarse a preguntar:

```text
¿Cuál es más rápido?
```

También debe responder:

```text
¿Cuánto espacio ahorra?
¿Cuánto cuesta construirlo?
¿Mantiene una FPR similar?
¿Cómo afecta readmissing y readrandom?
```

---

## 28. Limitaciones del experimento

- Se ejecutó una configuración controlada, no la configuración nativa completa de Speedb.
- Se utilizó un solo hilo.
- La carga usa valores de 1024 bytes.
- El write buffer es de 4 MiB.
- Ribbon puede utilizar una política híbrida entre flush y compactación.
- Los contadores de filtro representan consultas internas, no operaciones completas.
- Una única ejecución no es suficiente para obtener conclusiones estadísticas definitivas.
- Se requieren varias repeticiones para calcular media, desviación estándar e intervalos de confianza.

---

## 29. Estructura final de resultados

```text
results/
└── experiment_b/
    └── speedb/
        ├── speedb_bloom10_fillrandom.log
        ├── speedb_bloom10_fillrandom.time
        ├── speedb_bloom10_compact.log
        ├── speedb_bloom10_compact.time
        ├── speedb_bloom10_readmissing.log
        ├── speedb_bloom10_readmissing.time
        ├── speedb_bloom10_readrandom.log
        ├── speedb_bloom10_readrandom.time
        ├── speedb_bloom10_db_stats.log
        ├── speedb_bloom10_database_size.txt
        ├── speedb_ribbon10eq_fillrandom.log
        ├── speedb_ribbon10eq_fillrandom.time
        ├── speedb_ribbon10eq_compact.log
        ├── speedb_ribbon10eq_compact.time
        ├── speedb_ribbon10eq_readmissing.log
        ├── speedb_ribbon10eq_readmissing.time
        ├── speedb_ribbon10eq_readrandom.log
        ├── speedb_ribbon10eq_readrandom.time
        ├── speedb_ribbon10eq_db_stats.log
        ├── speedb_ribbon10eq_database_size.txt
        ├── speedb_bloom_vs_ribbon_summary.txt
        ├── speedb_bloom_vs_ribbon_resources_summary.txt
        ├── speedb_bloom_vs_ribbon_fpr_summary.txt
        └── speedb_bloom_vs_ribbon_report.txt
```

---

## 30. Recomendaciones de reproducibilidad

- Ejecutar Bloom y Ribbon en el mismo equipo.
- Mantener el equipo conectado a corriente.
- Evitar procesos pesados en segundo plano.
- Mantener la misma semilla.
- Eliminar cada base antes de crearla.
- Utilizar la misma instrumentación.
- Repetir el experimento varias veces.
- Alternar el orden Bloom/Ribbon entre repeticiones.
- Registrar temperatura, estado de caché y carga del sistema.
- Conservar los logs originales.
- No mezclar resultados de esta fase con la etapa anterior.
- Mantener los hashes Git de los motores utilizados.
