# Evaluación Comparativa de Filtros Probabilísticos (Bloom, Ribbon y Xor) en Motores Clave-Valor LSM-Tree

Este repositorio contiene el marco experimental, las implementaciones personalizadas de filtros probabilísticos (**Bloom Filter**, **Ribbon Filter** y **Xor Filter**) y la suite automatizada de pruebas para evaluar y comparar el impacto de estas estructuras sobre cuatro motores clave-valor basados en árboles LSM (*Log-Structured Merge-tree*): **LevelDB 1.23**, **PebblesDB v1.0**, **RocksDB 8.6.7** y **Speedb 2.8.0**.

---

## 1. Resumen del Proyecto

### Contexto y Motivación
En motores de almacenamiento LSM-tree, las lecturas de claves inexistentes o ubicadas en niveles profundos pueden requerir múltiples accesos a almacenamiento secundario (E/S a disco). Las estructuras probabilísticas de pertenencia aproximada (*Approximate Membership Queries*, AMQ) permiten filtrar en memoria principal (RAM) si una clave definitivamente no reside en una tabla SSTable dada antes de acceder al disco, reduciendo drásticamente la latencia y la amplificación de lectura.

Este proyecto evalúa de forma rigurosa cómo tres filtros representativos del estado del arte afectan el throughput, la latencia, el consumo de memoria y la sobrecarga de compactación en cuatro arquitecturas distintas:
1. **LevelDB 1.23**: Motor LSM clásico nivelado (*Leveled Compaction*) con filtros por bloque de datos (2 KB).
2. **PebblesDB v1.0**: Arquitectura *Fragmented LSM* (FLSM) basada en *guards* para reducir la amplificación de escritura a expensas de mayor amplificación de lectura.
3. **RocksDB 8.6.7**: Motor de grado de producción industrial con filtros completos por archivo SSTable (*Full Filter*) y compactación concurrente.
4. **Speedb 2.8.0**: Variante de RocksDB con optimizaciones *lock-free* y mitigación de contención de cerrojos bajo alta concurrencia.

### Estructuras Probabilísticas Evaluadas
* **Sin Filtro (*Baseline*)**: Ejecución nativa sin estructuras de filtrado probabilístico.
* **Bloom Filter**: Estructura estándar basada en $k$ funciones hash sobre un bit array (10 bits por clave, FPR $\approx 0.84\%$).
* **Ribbon Filter**: Filtro de compresión lineal basado en resolución gaussiana acotada de bandas (10 bpk equiv., consumo real $\approx 7.0$ bits/clave, FPR $\approx 0.78\%$, ahorro de espacio $\sim 30\%$).
* **Xor Filter**: Estructura basada en resolución hipergráfica 3-Xor (*peeling*) con huellas de 7 bits (10 bpk equiv., consumo real $\approx 8.6$ bits/clave, FPR $\approx 0.78\%$, ahorro de espacio $\sim 15\%$).

### Entorno Experimental
* **CPU:** 12th Gen Intel(R) Core(TM) i5-12600KF (16 núcleos lógicos, 20 MB CPU Cache L3)
* **Memoria RAM:** 32 GB DDR4
* **Almacenamiento:** Unidad de estado sólido NVMe
* **Sistema Operativo:** Linux Kernel 6.8 (x86_64)
* **Escala Base:** $N = 1{,}000{,}000$ operaciones de escritura / $1{,}000{,}000$ operaciones de lectura

---

## 2. Estructura del Repositorio

```text
Proyecto-SOA/
├── README.md                                  # Este documento (resumen general y guía)
├── benchmarks/                                # Suite automatizada de pruebas y reportes
│   ├── config.sh                              # Parámetros globales y detección de binarios
│   ├── run_all.sh                             # Orquestador general (ejecuta las 5 pruebas)
│   ├── clean_results.sh                       # Script de limpieza de datos temporales
│   ├── reporter.py                            # Parser de métricas y generador de tablas
│   ├── bench_01_readmissing.sh                # Prueba 01: Lecturas negativas (100% misses)
│   ├── bench_02_readrandom.sh                 # Prueba 02: Lecturas puntuales (hits)
│   ├── bench_03_fillrandom_space.sh           # Prueba 03: Ingesta masiva y huella en disco
│   ├── bench_04_readwhilewriting.sh           # Prueba 04: Concurrencia mixta multihilo
│   └── bench_05_seekrandom.sh                 # Prueba 05: Iteradores y búsquedas de rango
├── engines/                                   # Código fuente de los 4 motores KV
│   ├── leveldb-1.23/                          # LevelDB con Bloom, Ribbon (64-bit) y Xor
│   ├── pebblesdb-v1.0/                        # PebblesDB con Bloom, Ribbon y Xor
│   ├── rocksdb-8.6.7/                         # RocksDB con Bloom, Ribbon y 3-Xor corregido
│   └── speedb-2.8.0/                          # Speedb con Bloom, Ribbon y 3-Xor corregido
├── results/                                   # Resultados consolidados por motor
│   ├── resumen_leveldb.txt                    # Métricas de LevelDB (Pruebas 01 a 05)
│   ├── resumen_pebblesdb.txt                  # Métricas de PebblesDB (Pruebas 01 a 05)
│   ├── resumen_rocksdb.txt                    # Métricas de RocksDB (Pruebas 01 a 05)
│   └── resumen_speedb.txt                     # Métricas de Speedb (Pruebas 01 a 05)
└── Stages Analisis/                           # Informes técnicos y validación académica
    ├── ANALISIS_EXPERIMENTAL_Y_ESTADO_DEL_ARTE.md  # Análisis exhaustivo y literatura científica
    ├── STAGE_A_BLOOM_FILTER_RESULTS.md        # Evaluación previa Etapa A (Bloom)
    ├── STAGE_B_BLOOM_VS_RIBBON_RESULTS.md     # Evaluación previa Etapa B (Bloom vs Ribbon)
    └── STAGE _C_BLOOM_VS_RIBBON_VS_XOR (PEBBLES).md # Evaluación previa Etapa C (PebblesDB)
```

---

## 3. Compilación de los Motores

Antes de ejecutar las pruebas, asegúrate de que los binarios `db_bench` de cada motor estén compilados:

```bash
# 1. LevelDB 1.23
mkdir -p engines/leveldb-1.23/build
cmake -S engines/leveldb-1.23 -B engines/leveldb-1.23/build -DCMAKE_BUILD_TYPE=Release
cmake --build engines/leveldb-1.23/build --target db_bench -j$(nproc)

# 2. PebblesDB v1.0
cd engines/pebblesdb-v1.0
autoreconf -if
./configure --disable-shared
make -j$(nproc) db_bench
cd ../..

# 3. RocksDB 8.6.7
make -C engines/rocksdb-8.6.7 -j$(nproc) db_bench USE_RTTI=1

# 4. Speedb 2.8.0
make -C engines/speedb-2.8.0 -j$(nproc) db_bench USE_RTTI=1
```

---

## 4. Guía de Ejecución de la Suite de Benchmarks

### A. Parámetros Configurables
El archivo [`benchmarks/config.sh`](benchmarks/config.sh) centraliza los parámetros de las pruebas, los cuales pueden ser sobrescritos mediante variables de entorno:

| Variable | Valor por Defecto | Descripción |
| :--- | :--- | :--- |
| `NUM_KEYS` | `250000` (o `1000000`) | Cantidad total de claves a escribir |
| `NUM_READS` | `$NUM_KEYS` | Cantidad total de lecturas puntuales a ejecutar |
| `SEEK_OPS` | `NUM_KEYS / 5` | Cantidad de operaciones de posicionamiento `Seek` |
| `VALUE_SIZE` | `100` (100 Bytes) | Tamaño en bytes del valor de cada clave |
| `WRITE_BUFFER_SIZE` | `4194304` (4 MB) | Tamaño del MemTable antes de vaciar a L0 (*flush*) |
| `CACHE_SIZE` | `33554432` (32 MB) | Tamaño del *Block Cache* en memoria RAM |
| `BLOCK_SIZE` | `4096` (4 KB) | Tamaño de cada bloque de datos en las SSTables |

### B. Ejecución Completa (Suite de 5 Pruebas)
Para ejecutar las 5 pruebas de forma secuencial sobre todos los motores y variantes:

```bash
# Limpiar resultados y bases de datos temporales previas
./benchmarks/clean_results.sh

# Ejecutar la suite completa a escala de 1M operaciones
NUM_KEYS=1000000 ./benchmarks/run_all.sh
```

### C. Ejecución Individual por Tipo de Carga
Cada prueba es un script independiente que ejecuta los 4 motores (`leveldb`, `pebblesdb`, `rocksdb`, `speedb`) con las 4 variantes de filtro (`nofilter`, `bloom`, `ribbon`, `xor`) y actualiza el archivo de resumen consolidado en `results/resumen_<motor>.txt`:

```bash
# 1. Prueba 01: Búsquedas Negativas (100% Misses)
# Evalúa el filtrado en RAM ante claves inexistentes para evitar E/S a disco
./benchmarks/bench_01_readmissing.sh

# 2. Prueba 02: Búsquedas Positivas (Lecturas puntuales de claves presentes)
# Evalúa la sobrecarga de cómputo del hashing sobre claves existentes
./benchmarks/bench_02_readrandom.sh

# 3. Prueba 03: Ingesta Masiva y Espacio (fillrandom con claves de 1 KB)
# Mide el costo de CPU de construcción de filtros en compactaciones y el tamaño final en disco
./benchmarks/bench_03_fillrandom_space.sh

# 4. Prueba 04: Concurrencia Extrema (readwhilewriting con 4 hilos concurrentes)
# Evalúa la contención de bloqueos entre lectores concurrentes y constructores de filtros
./benchmarks/bench_04_readwhilewriting.sh

# 5. Prueba 05: Búsquedas de Rango con Iteradores (seekrandom)
# Evalúa si los filtros aportan beneficio o sobrecarga en cursores Seek(key)
./benchmarks/bench_05_seekrandom.sh
```

---

## 5. Resumen de Resultados Experimentales

A continuación se resumen las mediciones obtenidas con $N = 1{,}000{,}000$ operaciones en el entorno de pruebas:

### Prueba 01: Búsquedas Negativas (100% Misses — `readmissing`)
Representa el escenario de **máxima exigencia para el filtro**: el 100% de las consultas buscan claves que no existen.

| Motor KV | Sin Filtro (Ops/s) | Bloom (Ops/s) | Ribbon (Ops/s) | Xor (Ops/s) | Latencia Sin Filtro | Latencia Con Filtro (Promedio) | Factor de Aceleración |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Speedb 2.8.0** | 184,028 | **1,545,291** | 1,437,961 | 1,359,382 | 5.434 µs | 0.693 µs | **8.4x (+740%)** |
| **PebblesDB v1.0** | 163,988 | **1,233,046** | 861,326 | 1,191,895 | 6.098 µs | 0.937 µs | **7.5x (+652%)** |
| **RocksDB 8.6.7** | 164,556 | **829,018** | 782,554 | 769,563 | 6.077 µs | 1.261 µs | **5.0x (+404%)** |
| **LevelDB 1.23** | 498,753 | **1,089,325** | 1,085,776 | 991,080 | 2.005 µs | 0.949 µs | **2.2x (+118%)** |

* **Conclusión:** La ausencia de filtro degrada el rendimiento de 2x a 8.4x. **Bloom** ofrece el throughput pico por simplicidad de hash directo, pero **Ribbon** alcanza un rendimiento prácticamente idéntico con **30% menos memoria**.

---

### Prueba 02: Búsquedas Positivas (`readrandom`)
Evalúa el comportamiento de los filtros ante lecturas de claves que compiten con el árbol LSM.

| Motor KV | Sin Filtro (Ops/s) | Bloom (Ops/s) | Ribbon (Ops/s) | Xor (Ops/s) | Latencia Sin Filtro | Latencia Mejor Filtro | Impacto del Filtro |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Speedb 2.8.0** | 198,700 | 477,850 | **480,276** | 466,171 | 5.033 µs | 2.082 µs (Ribbon) | **+141% (+2.4x)** |
| **RocksDB 8.6.7** | 174,134 | 370,249 | 374,473 | **381,221** | 5.743 µs | 2.623 µs (Xor) | **+119% (+2.2x)** |
| **PebblesDB v1.0** | 269,906 | 485,909 | 434,972 | **486,618** | 3.705 µs | 2.055 µs (Xor) | **+80% (+1.8x)** |
| **LevelDB 1.23** | 529,942 | 589,623 | **629,723** | 609,013 | 1.887 µs | 1.588 µs (Ribbon) | **+19% (+1.2x)** |

* **Conclusión:** En LSM-trees, el filtro acelera incluso lecturas de claves existentes al descartar rápidamente los SSTables de niveles superiores donde la clave no está. **Xor y Ribbon superan a Bloom** en lecturas positivas gracias a su mayor compacidad y menor presión sobre la caché L2/L3.

---

### Prueba 03: Ingesta Masiva y Huella en Disco (`fillrandom` con 1,000,000 claves de 1 KB)
Mide el costo de CPU durante la construcción de filtros en compactaciones de fondo frente al tamaño de almacenamiento en disco.

| Motor KV | Métrica | Sin Filtro | Bloom | Ribbon | Xor |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **PebblesDB v1.0** | Throughput Escritura | **119.3 MB/s** | 114.4 MB/s | 92.3 MB/s | **80.7 MB/s** |
| | Tamaño Base de Datos | 891.6 MB | **844.4 MB** | 880.4 MB | 874.6 MB |
| **Speedb 2.8.0** | Throughput Escritura | **73.7 MB/s** | 72.3 MB/s | 71.9 MB/s | 72.1 MB/s |
| | Tamaño Base de Datos | 435.2 MB | **407.5 MB** | 413.8 MB | 469.9 MB |
| **LevelDB 1.23** | Throughput Escritura | 52.6 MB/s | 52.7 MB/s | 53.3 MB/s | **53.9 MB/s** |
| | Tamaño Base de Datos | **429.8 MB** | 435.1 MB | 436.0 MB | 436.0 MB |
| **RocksDB 8.6.7** | Throughput Escritura | **52.9 MB/s** | 51.1 MB/s | 50.8 MB/s | 49.3 MB/s |
| | Tamaño Base de Datos | 396.6 MB | 394.9 MB | **393.2 MB** | 396.0 MB |

* **Conclusión:** La sobrecarga computacional de resolver sistemas lineales (Ribbon) o pelar grafos (Xor) **solo se manifiesta en motores con ingesta acelerada (PebblesDB FLSM)**. En motores convencionales limitados por I/O a disco (RocksDB/LevelDB), el costo computacional de Ribbon y Xor queda completamente absorbido por la latencia de almacenamiento.

---

### Prueba 04: Concurrencia Extrema (`readwhilewriting` con 4 hilos concurrentes)
Simula un entorno de alta carga donde múltiples lectores compiten con hilos de fondo que construyen filtros y compactan datos.

| Motor KV | Sin Filtro (Ops/s) | Bloom (Ops/s) | Ribbon (Ops/s) | Xor (Ops/s) | Latencia Sin Filtro | Latencia Mejor Filtro | Factor de Aceleración |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Speedb 2.8.0** | 1,353,581 | **7,959,961** | 7,785,206 | 6,711,127 | 2.769 µs | 0.486 µs (Bloom) | **5.8x más rápido** |
| **RocksDB 8.6.7** | 406,151 | **2,370,210** | 2,300,243 | 2,158,539 | 9.757 µs | 1.674 µs (Bloom) | **5.8x más rápido** |
| **LevelDB 1.23** | 135,062 | **232,126** | 225,276 | 226,449 | 7.404 µs | 4.308 µs (Bloom) | **1.7x más rápido** |
| **PebblesDB v1.0** | *Inestable* | 139,606 | 137,476 | **145,539** | - | 6.871 µs (Xor) | **Estabilidad con filtros** |

* **Conclusión:** **Speedb** demuestra una superioridad contundente alcanzando casi **8 millones de ops/segundo**, beneficiándose de su arquitectura *lock-free* una vez que los filtros eliminan el cuello de botella de E/S.

---

### Prueba 05: Búsquedas de Rango con Iteradores (`seekrandom`, 200,000 seeks)
Evalúa el comportamiento de los filtros ante operaciones de posicionamiento de cursores e iteradores (`Seek`).

| Motor KV | Sin Filtro (Ops/s) | Bloom (Ops/s) | Ribbon (Ops/s) | Xor (Ops/s) | Mejor Variante |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **LevelDB 1.23** | 249,066 | 180,440 | **268,601** | 211,954 | **Ribbon (3.72 µs/op)** |
| **RocksDB 8.6.7** | 132,175 | 134,982 | 125,605 | **151,428** | **Xor (6.60 µs/op)** |
| **PebblesDB v1.0** | 138,160 | 171,233 | **174,277** | 145,921 | **Ribbon (5.74 µs/op)** |
| **Speedb 2.8.0** | 158,091 | 154,858 | 153,242 | 150,822 | Parejos (~150K–158K ops/s) |

* **Conclusión:** Las operaciones `Seek(key)` buscan la primera clave mayor o igual ($\ge key$) en los bloques de datos; el costo de posicionamiento en el bloque de índice domina sobre el filtro.

---

## 6. Documentación Detallada y Referencias

Para un análisis técnico profundo, demostraciones matemáticas rigurosas (modelo Monkey de Dayan et al., límites de información de Dillinger & Lemire) y justificación teórica de los resultados, consulta los informes en la carpeta [`Stages Analisis/`](Stages%20Analisis/):

* [`Stages Analisis/ANALISIS_EXPERIMENTAL_Y_ESTADO_DEL_ARTE.md`](Stages%20Analisis/ANALISIS_EXPERIMENTAL_Y_ESTADO_DEL_ARTE.md): **Informe Principal** con análisis pormenorizado, modelado analítico y contraste con papers en conferencias ACM SIGMOD, SOSP y USENIX FAST.
* [`Stages Analisis/STAGE_A_BLOOM_FILTER_RESULTS.md`](Stages%20Analisis/STAGE_A_BLOOM_FILTER_RESULTS.md): Resultados de la Etapa A (Línea base Bloom).
* [`Stages Analisis/STAGE_B_BLOOM_VS_RIBBON_RESULTS.md`](Stages%20Analisis/STAGE_B_BLOOM_VS_RIBBON_RESULTS.md): Resultados de la Etapa B (Bloom vs Ribbon).
* [`Stages Analisis/STAGE _C_BLOOM_VS_RIBBON_VS_XOR (PEBBLES).md`](Stages%20Analisis/STAGE%20_C_BLOOM_VS_RIBBON_VS_XOR%20(PEBBLES).md): Resultados de la Etapa C (PebblesDB FLSM).
