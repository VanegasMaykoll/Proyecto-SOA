# LevelDB 1.23 — Experimento B: Bloom Filter vs Ribbon Filter

Este documento describe el procedimiento para comparar, dentro de **LevelDB 1.23**, dos estructuras probabilísticas de pertenencia asociadas a las SSTables:

- **Bloom Filter**
- **Ribbon Filter** (Añadido mediante una extensión personalizada)

La comparación se diseñó de forma controlada, manteniendo constantes la carga, el tamaño de caché, la compresión y el resto de parámetros del motor.

La única variable experimental modificada fue el tipo de filtro probabilístico: Bloom Filter frente a Ribbon Filter.

---

## 1. Objetivo del experimento

El objetivo fue evaluar cómo cambia el comportamiento de LevelDB al sustituir Bloom Filter por Ribbon Filter bajo una carga idéntica. 

Las dimensiones analizadas son:
- Tiempo de construcción y compactación.
- Latencia de consultas inexistentes (`readmissing`).
- Latencia de consultas exitosas (`readrandom`).
- Throughput.

---

## 2. Pregunta experimental

> ¿Cómo cambia el rendimiento, el consumo de recursos y el tamaño de almacenamiento de LevelDB 1.23 cuando se utiliza Ribbon Filter en lugar de Bloom Filter, manteniendo el resto de la configuración constante?

---

## 3. Hipótesis general

Ribbon Filter está diseñado para utilizar menos espacio permanente que Bloom Filter manteniendo una tasa de falsos positivos equivalente. 
A cambio, puede requerir mayor costo computacional (CPU) durante la construcción de la SSTable. La ventaja principal es el ahorro de espacio en disco/memoria (hasta ~30% menos para el filtro) a cambio de mayor latencia de escritura.

---

## 4. Requisito de compilación

LevelDB debe estar compilado en modo optimizado (Release). 

Como se agregó soporte para Ribbon Filter, el binario `db_bench` debe ser recompilado con `cmake`:

```bash
cd engines/leveldb-1.23
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

El ejecutable estará ubicado en: `engines/leveldb-1.23/build/db_bench` (o `db_bench.exe` en Windows).

---

## 5. Configuración experimental común

Configura las variables de entorno en tu consola. (Nota: en Windows PowerShell usa `$env:VAR = "valor"`).

Para Bash/Git Bash:
```bash
export KVROOT="$HOME/kv_engines_clean"  # O la ruta raíz de tu proyecto

export NUM=1000000
export READS=1000000
export VALUE_SIZE=1024
export THREADS=1

export CACHE_SIZE=$((32 * 1024 * 1024))
export WRITE_BUFFER_SIZE=$((4 * 1024 * 1024))

export FILTER_BITS=10
export COMPRESSION_RATIO=1.0

export LEVELDB_DATA="$KVROOT/data/experiment_b/leveldb"
export LEVELDB_RESULTS="$KVROOT/results/experiment_b/leveldb"

mkdir -p "$LEVELDB_DATA"
mkdir -p "$LEVELDB_RESULTS"
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

---

## 6. Ejecutar Bloom y Ribbon

A continuación se muestra el script de ejecución para Bash. Este script ejecuta las fases: `fillrandom`, `compact`, `readmissing` y `readrandom`.

```bash
BIN="$KVROOT/engines/leveldb-1.23/build/db_bench"

for FILTER in bloom10 ribbon10eq; do

  if [ "$FILTER" = "bloom10" ]; then
    USE_RIBBON=0
    FILTER_NAME="Bloom Filter"
  else
    USE_RIBBON=1
    FILTER_NAME="Ribbon Filter, Bloom-10 equivalent"
  fi

  DB="$LEVELDB_DATA/leveldb_${FILTER}"
  OUT="$LEVELDB_RESULTS/leveldb_${FILTER}"

  echo "============================================================"
  echo "LevelDB 1.23 — Experimento B"
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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_fillrandom.log"

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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
      --db="$DB" \
    2>&1 | tee "${OUT}_compact.log"

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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readmissing.log"

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
      --compression_ratio="$COMPRESSION_RATIO" \
      --bloom_bits="$FILTER_BITS" \
      --use_ribbon_filter="$USE_RIBBON" \
      --histogram=1 \
      --db="$DB" \
    2>&1 | tee "${OUT}_readrandom.log"

done
```

---

## 7. Verificación de Tamaño del Filtro y Base de Datos

Después de ejecutar el script, puedes comparar el tamaño total del directorio de datos para cada experimento, esto revelará el ahorro de espacio que proporciona Ribbon Filter sobre Bloom Filter para la misma carga:

```bash
du -sh "$LEVELDB_DATA"/leveldb_*
```
