# Avance del Proyecto SOA — Experimento C en PebblesDB

**Fecha:** 7 de agosto de 2026  
**Motor evaluado:** PebblesDB v1.0 / LevelDB 1.17  
**Experimento:** C — Comparación de filtros probabilísticos  
**Estructuras evaluadas:** Sin filtro, Bloom, Ribbon y Xor

---

## 1. Objetivo del avance

En este avance se preparó y validó el protocolo definitivo del **Experimento C sobre PebblesDB**, cuyo objetivo es comparar el comportamiento de distintas estructuras de filtrado probabilístico durante operaciones de escritura y lectura.

Las variantes evaluadas fueron:

- **Sin filtro**, como línea base.
- **Bloom**, implementación original disponible en PebblesDB/LevelDB.
- **Ribbon**, implementación integrada al proyecto.
- **Xor**, implementación integrada al proyecto.

Antes de ejecutar la comparación final fue necesario revisar el comportamiento de la compactación de PebblesDB, debido a que las primeras pruebas mostraban resultados incorrectos después de ejecutar una compactación manual.

---

## 2. Problema detectado durante la validación

El protocolo inicial ejecutaba:

```text
fillrandom
    ↓
compact
    ↓
readmissing
    ↓
readrandom
```

La operación `compact` de `db_bench` termina llamando internamente a:

```cpp
db_->CompactRange(NULL, NULL);
```

Durante las pruebas de control **sin ningún filtro** se observó:

| Estado | `readrandom` | `seekrandom` |
|---|---:|---:|
| Antes de `CompactRange()` | 1,000,000 / 1,000,000 | 1,000,000 / 1,000,000 |
| Después de `CompactRange()` | 122,791 / 1,000,000 | 945,537 / 1,000,000 |

Esto demostró que el problema no provenía de Bloom, Ribbon o Xor, sino de la ruta de compactación manual de PebblesDB.

### 2.1 Hallazgo en el código

Al revisar `db_impl.cc` y `version_set.cc` se encontró que la compactación automática y la compactación manual utilizan rutas distintas.

En la compactación automática, PebblesDB emplea funciones como:

```cpp
PickCompactionLevel(...)
PickCompactionForGuards(...)
```

y trabaja con la información de los **guards** utilizados para dividir correctamente los archivos SSTables.

En la ruta manual aparece explícitamente:

```cpp
// TODO Handle CompactRange method for guards
```

La compactación manual puede generar SSTables que atraviesan varios rangos de guards sin dividirse correctamente. Posteriormente, la metadata puede asociar el archivo únicamente a un guard, provocando que `DB::Get()` no consulte archivos que físicamente contienen la clave buscada.

Por esta razón se decidió **no modificar internamente PebblesDB** y ejecutar el Experimento C utilizando su mecanismo normal de compactación automática.

---

## 3. Validación de la compactación automática

Se creó una base nueva sin filtros y se ejecutaron las siguientes operaciones sin utilizar `CompactRange()`:

```text
fillrandom
    ↓
readrandom
    ↓
seekrandom
    ↓
stats + sstables
```

Los resultados fueron:

```text
readrandom:
1,000,000 of 1,000,000 found

seekrandom:
1,000,000 of 1,000,000 found
```

La estructura de la base quedó distribuida naturalmente en varios niveles:

| Nivel | SSTables | Tamaño aproximado |
|---:|---:|---:|
| L4 | 2 | 90 MiB |
| L5 | 7 | 422 MiB |
| L6 | 15 | 268 MiB |

Los guards y los SSTables conservaron una organización coherente y no se reprodujo el problema observado con la compactación manual.

### Conclusión de esta etapa

Para PebblesDB, el Experimento C debe ejecutarse **sin la operación manual `compact`**. La compactación se deja a cargo del propio motor durante el workload.

---

## 4. Protocolo definitivo del Experimento C

El protocolo aplicado a cada variante es:

```text
fillrandom
    ↓
readrandom_validation
    ↓
readmissing
    ↓
readrandom
    ↓
stats + sstables
```

La prueba `readrandom_validation` se utiliza como control de corrección y debe retornar:

```text
1,000,000 of 1,000,000 found
```

Si una variante no cumple esta condición, la ejecución debe considerarse inválida.

---

## 5. Parámetros del experimento

Todas las variantes se ejecutaron con:

```bash
--num=1000000
--reads=1000000
--value_size=1024
--threads=1
--write_threads=1
--read_threads=1
--compression_ratio=1.0
--write_buffer_size=4194304
--cache_size=33554432
--block_size=4096
```

Configuración de filtros:

| Variante | `--bloom_bits` | `--use_ribbon_filter` | `--use_xor_filter` |
|---|---:|---:|---:|
| No filter | -1 | 0 | 0 |
| Bloom | 10 | 0 | 0 |
| Ribbon | 10 | 1 | 0 |
| Xor | 10 | 0 | 1 |

---

## 6. Preparación del directorio de resultados

```bash
cd ~/Proyecto-SOA-local/engines/pebblesdb-v1.0

ROOT="/home/maykoll_vanegas/Proyecto-SOA-local/Resultados/Experimento_C_Final"
RUN="$ROOT/run_01"

mkdir -p   "$RUN/nofilter/logs" "$RUN/nofilter/database" "$RUN/nofilter/metadata"   "$RUN/bloom/logs"    "$RUN/bloom/database"    "$RUN/bloom/metadata"   "$RUN/ribbon/logs"   "$RUN/ribbon/database"   "$RUN/ribbon/metadata"   "$RUN/xor/logs"      "$RUN/xor/database"      "$RUN/xor/metadata"
```

Estructura:

```text
Experimento_C_Final/
└── run_01/
    ├── nofilter/
    │   ├── database/
    │   ├── logs/
    │   └── metadata/
    ├── bloom/
    ├── ribbon/
    └── xor/
```

---

## 7. Archivo de configuración

```bash
cat > "$ROOT/config.sh" <<'EOF'
ROOT="/home/maykoll_vanegas/Proyecto-SOA-local/Resultados/Experimento_C_Final"
RUN="$ROOT/run_01"

COMMON=(
  --num=1000000
  --reads=1000000
  --value_size=1024
  --threads=1
  --write_threads=1
  --read_threads=1
  --compression_ratio=1.0
  --write_buffer_size=4194304
  --cache_size=33554432
  --block_size=4096
)

NOFILTER=(
  --bloom_bits=-1
  --use_ribbon_filter=0
  --use_xor_filter=0
)

BLOOM=(
  --bloom_bits=10
  --use_ribbon_filter=0
  --use_xor_filter=0
)

RIBBON=(
  --bloom_bits=10
  --use_ribbon_filter=1
  --use_xor_filter=0
)

XOR=(
  --bloom_bits=10
  --use_ribbon_filter=0
  --use_xor_filter=1
)
EOF
```

En una terminal nueva:

```bash
source "/home/maykoll_vanegas/Proyecto-SOA-local/Resultados/Experimento_C_Final/config.sh"
```

---

## 8. Ejecución de las pruebas

### 8.1 Sin filtro

```bash
VARIANT="nofilter"
BASE="$RUN/$VARIANT"
LOGS="$BASE/logs"
DB="$BASE/database/db"

rm -rf "$DB"
mkdir -p "$LOGS" "$BASE/database" "$BASE/metadata"
```

#### `fillrandom`

```bash
/usr/bin/time -v ./db_bench   --benchmarks=fillrandom   "${COMMON[@]}"   "${NOFILTER[@]}"   --db="$DB"   2>&1 | tee "$LOGS/01_fillrandom.log"

du -sb "$DB" | tee "$BASE/metadata/size_after_fillrandom.txt"
```

#### Validación

```bash
/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${NOFILTER[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/02_readrandom_validation.log"
```

#### `readmissing`

```bash
/usr/bin/time -v ./db_bench   --benchmarks=readmissing   "${COMMON[@]}"   "${NOFILTER[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/03_readmissing.log"
```

#### `readrandom`

```bash
/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${NOFILTER[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/04_readrandom.log"
```

#### Estado final

```bash
/usr/bin/time -v ./db_bench   --benchmarks=stats,sstables   "${COMMON[@]}"   "${NOFILTER[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/05_structure.log"

du -sb "$DB" | tee "$BASE/metadata/size_final.txt"
```

---

### 8.2 Bloom

```bash
VARIANT="bloom"
BASE="$RUN/$VARIANT"
LOGS="$BASE/logs"
DB="$BASE/database/db"

rm -rf "$DB"
mkdir -p "$LOGS" "$BASE/database" "$BASE/metadata"
```

```bash
/usr/bin/time -v ./db_bench   --benchmarks=fillrandom   "${COMMON[@]}"   "${BLOOM[@]}"   --db="$DB"   2>&1 | tee "$LOGS/01_fillrandom.log"

du -sb "$DB" | tee "$BASE/metadata/size_after_fillrandom.txt"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${BLOOM[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/02_readrandom_validation.log"

/usr/bin/time -v ./db_bench   --benchmarks=readmissing   "${COMMON[@]}"   "${BLOOM[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/03_readmissing.log"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${BLOOM[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/04_readrandom.log"

/usr/bin/time -v ./db_bench   --benchmarks=stats,sstables   "${COMMON[@]}"   "${BLOOM[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/05_structure.log"

du -sb "$DB" | tee "$BASE/metadata/size_final.txt"
```

---

### 8.3 Ribbon

```bash
VARIANT="ribbon"
BASE="$RUN/$VARIANT"
LOGS="$BASE/logs"
DB="$BASE/database/db"

rm -rf "$DB"
mkdir -p "$LOGS" "$BASE/database" "$BASE/metadata"
```

```bash
/usr/bin/time -v ./db_bench   --benchmarks=fillrandom   "${COMMON[@]}"   "${RIBBON[@]}"   --db="$DB"   2>&1 | tee "$LOGS/01_fillrandom.log"

du -sb "$DB" | tee "$BASE/metadata/size_after_fillrandom.txt"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${RIBBON[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/02_readrandom_validation.log"

/usr/bin/time -v ./db_bench   --benchmarks=readmissing   "${COMMON[@]}"   "${RIBBON[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/03_readmissing.log"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${RIBBON[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/04_readrandom.log"

/usr/bin/time -v ./db_bench   --benchmarks=stats,sstables   "${COMMON[@]}"   "${RIBBON[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/05_structure.log"

du -sb "$DB" | tee "$BASE/metadata/size_final.txt"
```

---

### 8.4 Xor

```bash
VARIANT="xor"
BASE="$RUN/$VARIANT"
LOGS="$BASE/logs"
DB="$BASE/database/db"

rm -rf "$DB"
mkdir -p "$LOGS" "$BASE/database" "$BASE/metadata"
```

```bash
/usr/bin/time -v ./db_bench   --benchmarks=fillrandom   "${COMMON[@]}"   "${XOR[@]}"   --db="$DB"   2>&1 | tee "$LOGS/01_fillrandom.log"

du -sb "$DB" | tee "$BASE/metadata/size_after_fillrandom.txt"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${XOR[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/02_readrandom_validation.log"

/usr/bin/time -v ./db_bench   --benchmarks=readmissing   "${COMMON[@]}"   "${XOR[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/03_readmissing.log"

/usr/bin/time -v ./db_bench   --benchmarks=readrandom   "${COMMON[@]}"   "${XOR[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/04_readrandom.log"

/usr/bin/time -v ./db_bench   --benchmarks=stats,sstables   "${COMMON[@]}"   "${XOR[@]}"   --use_existing_db=1   --db="$DB"   2>&1 | tee "$LOGS/05_structure.log"

du -sb "$DB" | tee "$BASE/metadata/size_final.txt"
```

---

## 9. Script de resumen

El resumen puede generarse con:

```bash
"$ROOT/summarize_experiment_c_final.sh"
```

y se guarda en:

```text
/home/maykoll_vanegas/Proyecto-SOA-local/Resultados/Experimento_C_Final/run_01/resumen_metricas.txt
```

---

## 10. Resultados obtenidos

### 10.1 Métricas principales

| Variante | `fillrandom` µs/op | `fillrandom` MB/s | `readmissing` µs/op | `readrandom` final µs/op | DB final |
|---|---:|---:|---:|---:|---:|
| Sin filtro | **8.260** | **120.1** | 4.317 | 2.797 | 817,596,875 B |
| Bloom | 8.581 | 115.6 | **0.413** | **2.448** | 837,166,646 B |
| Ribbon | 9.481 | 104.6 | 0.875 | 2.715 | 852,114,634 B |
| Xor | 8.849 | 112.1 | 0.462 | 2.464 | 852,911,659 B |

### 10.2 Validación de corrección

| Variante | Resultado |
|---|---|
| Sin filtro | 1,000,000 / 1,000,000 |
| Bloom | 1,000,000 / 1,000,000 |
| Ribbon | 1,000,000 / 1,000,000 |
| Xor | 1,000,000 / 1,000,000 |

Todas las variantes pasaron la validación.

---

## 11. Resumen de resultados

### Búsquedas negativas (`readmissing`)

```text
No filter : 4.317 µs/op
Bloom     : 0.413 µs/op
Ribbon    : 0.875 µs/op
Xor       : 0.462 µs/op
```

Frente a no utilizar filtro:

| Filtro | Reducción aproximada de latencia | Aceleración aproximada |
|---|---:|---:|
| Bloom | 90.4 % | 10.45× |
| Xor | 89.3 % | 9.34× |
| Ribbon | 79.7 % | 4.93× |

Bloom obtuvo el mejor resultado de este run en búsquedas negativas, seguido de cerca por Xor. Ribbon también presentó una mejora clara frente al caso sin filtro.

### Búsquedas positivas (`readrandom`)

```text
No filter : 2.797 µs/op
Bloom     : 2.448 µs/op
Ribbon    : 2.715 µs/op
Xor       : 2.464 µs/op
```

Bloom y Xor tuvieron resultados prácticamente equivalentes. En esta ejecución, Bloom redujo aproximadamente un 12.5 % la latencia frente a la línea base y Xor aproximadamente un 11.9 %.

### Costo de escritura

```text
No filter : 8.260 µs/op
Bloom     : 8.581 µs/op
Xor       : 8.849 µs/op
Ribbon    : 9.481 µs/op
```

Sobrecosto aproximado frente a no utilizar filtros:

| Filtro | Sobrecosto de escritura |
|---|---:|
| Bloom | +3.9 % |
| Xor | +7.1 % |
| Ribbon | +14.8 % |

---

## 12. Interpretación preliminar

En esta primera ejecución, **Bloom presentó el mejor equilibrio general** entre costo de escritura y latencia de lectura.

Xor mostró un comportamiento especialmente competitivo:

- Resultado muy cercano a Bloom en `readmissing`.
- Resultado prácticamente igual a Bloom en `readrandom`.
- Sobrecosto de escritura moderado.
- Validación completa sin falsos negativos.

Ribbon también redujo considerablemente el costo de las búsquedas negativas respecto al caso sin filtro, pero presentó un mayor costo de construcción y una latencia de lectura superior a Bloom y Xor.

Ranking preliminar de este run:

```text
Búsqueda negativa:
Bloom > Xor >> Ribbon > Sin filtro

Búsqueda positiva:
Bloom ≈ Xor > Ribbon > Sin filtro

Costo de escritura:
Sin filtro > Bloom > Xor > Ribbon
```

En esta representación, `>` significa mejor comportamiento para la métrica correspondiente.

---

## 13. Consideraciones metodológicas

Estos resultados corresponden a **una única ejecución (`run_01`)**, por lo que todavía no deben interpretarse como diferencias estadísticamente definitivas.

La prueba:

```text
02_readrandom_validation
```

debe utilizarse únicamente para verificar corrección y no como métrica principal de rendimiento, ya que durante esa etapa todavía puede existir actividad de mantenimiento automático del motor.

Las métricas principales de lectura se obtienen de:

```text
03_readmissing
04_readrandom
```

El tamaño total de la base tampoco debe interpretarse directamente como el tamaño del filtro, porque PebblesDB utiliza compactación automática y las distintas ejecuciones pueden finalizar con configuraciones ligeramente diferentes de SSTables y niveles.

---

## 14. Siguiente etapa recomendada

1. Repetir el mismo protocolo al menos **5 veces** con los mismos parámetros.
2. Calcular media, mediana, desviación estándar, mínimo y máximo.
3. Comparar principalmente `fillrandom`, `readmissing` y `readrandom`.
4. Mantener la validación obligatoria de `1,000,000/1,000,000`.
5. Posteriormente repetir con otros volúmenes de datos para evaluar escalabilidad.

---

## 15. Conclusión del avance

Este avance permitió:

- Identificar que los errores iniciales no provenían de Ribbon o Xor.
- Aislar el problema en la ruta de compactación manual `CompactRange()` de PebblesDB.
- Comprobar que la compactación automática mantiene la consistencia de las búsquedas.
- Redefinir el protocolo del Experimento C para utilizar el comportamiento normal del motor.
- Ejecutar correctamente las cuatro variantes.
- Validar que todas retornan el 100 % de las claves esperadas.
- Obtener una primera comparación de rendimiento entre Bloom, Ribbon y Xor.

Los resultados preliminares muestran que **Bloom y Xor son las alternativas más competitivas en PebblesDB bajo esta carga**, mientras que Ribbon ofrece una mejora clara respecto al caso sin filtro, pero con mayor costo de construcción y consulta.
