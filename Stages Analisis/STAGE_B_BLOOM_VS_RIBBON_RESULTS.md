# Etapa B — Evaluación de Bloom Filter frente a Ribbon Filter

## 1. Propósito

La etapa B evaluó dos filtros probabilísticos disponibles de forma nativa en motores derivados de RocksDB:

- **Bloom Filter**
- **Ribbon Filter**

Los motores utilizados fueron:

- RocksDB 8.6.7
- Speedb 2.8.0

La comparación se diseñó para modificar únicamente el tipo de filtro asociado a las SSTables. El resto de la carga, la configuración de la memtable, la compresión, la caché, la semilla y la instrumentación se mantuvieron constantes.

La condición Ribbon se denominó:

```text
ribbon10eq
```

porque se configuró con una precisión objetivo equivalente a Bloom de 10 bits por clave. El valor no debe interpretarse como un consumo físico exacto de 10 bits por clave para Ribbon.

> Los porcentajes se calculan usando Bloom 10 como referencia. En latencia y tiempo, un porcentaje negativo representa una mejora de Ribbon.

---

## 2. Pregunta experimental

La pregunta principal de la etapa fue:

> ¿Cómo cambia el rendimiento, el consumo de recursos, la tasa de falsos positivos y el tamaño de almacenamiento cuando Bloom Filter es reemplazado por Ribbon Filter bajo una carga equivalente?

La comparación se concentró en:

- Costo de construcción durante `fillrandom`.
- Costo de compactación.
- Latencia y throughput de búsquedas inexistentes.
- Latencia y throughput de búsquedas aleatorias.
- Falsos positivos.
- Comparaciones internas y bloques leídos.
- Consumo máximo de memoria.
- Tamaño final de la base.

---

## 3. Equipo utilizado

| Recurso | Especificación |
|---|---|
| Fabricante / plataforma | ASUS, identificada por Ubuntu como `ASUS System Product Name` |
| Procesador | Intel Core i5-12600KF de 12.ª generación |
| Procesadores lógicos reportados | 16 |
| Memoria RAM | 32.0 GiB |
| Almacenamiento | SSD M.2 de 2 TB |
| Gráficos | GPU NVIDIA; modelo exacto no registrado |
| Sistema operativo | Ubuntu 22.04.5 LTS |
| Arquitectura | 64 bits |
| Entorno de escritorio | GNOME 42.9 |
| Sistema de ventanas | X11 |
| Caché de CPU reportada por `db_bench` | 20,480 KB |

El modelo exacto del SSD M.2 y su interfaz no quedaron registrados, por lo que el informe no asume si corresponde a SATA o NVMe.

---

## 4. Versiones de los motores

| Motor | Versión | Commit |
|---|---|---|
| RocksDB | v8.6.7 | `cb7a5e02edeb883193eb5b4901d5943f58e9add9` |
| Speedb | 2.8.0 | `c328f0ebc33db0166cd2a2844ff44c8034e30e5b` |

---

## 5. Diseño experimental

### 5.1 Condiciones

#### Bloom

```bash
--bloom_bits=10
--use_ribbon_filter=0
```

#### Ribbon

```bash
--bloom_bits=10
--use_ribbon_filter=1
```

En la segunda condición, el parámetro representa una precisión equivalente a Bloom 10.

### 5.2 Decisiones de control

En ambos motores se mantuvo:

```bash
--memtablerep=skip_list
--memtable_bloom_size_ratio=0
--whole_key_filtering=1
```

Esto permitió:

- Mantener la misma estructura de memtable.
- Evitar un Bloom adicional en la memtable.
- Evaluar filtros aplicados sobre claves completas.
- Reducir la posibilidad de atribuir a Ribbon cambios causados por otra configuración.

En Speedb también se utilizó:

```bash
--enable_speedb_features=0
--use_spdb_writes=0
```

Estas opciones evitaron que optimizaciones específicas de Speedb cambiaran simultáneamente con el filtro.

### 5.3 Política Ribbon

La política seleccionada por `db_bench` puede utilizar Bloom en archivos producidos directamente por flush y Ribbon en salidas posteriores de compactación. Por este motivo se ejecutó una compactación completa antes de realizar las lecturas:

```text
fillrandom
→ compact
→ readmissing
→ readrandom
```

El experimento evalúa, por tanto, la **política Ribbon nativa del motor**, no una implementación modificada para forzar Ribbon puro en absolutamente todos los archivos.

---

## 6. Parámetros comunes

| Parámetro | Valor |
|---|---:|
| Registros | 1,000,000 |
| Lecturas | 1,000,000 |
| Tamaño del valor | 1,024 bytes |
| Hilos solicitados | 1 |
| Block cache | 32 MiB |
| Write buffer | 4 MiB |
| Compresión | Snappy |
| Compression ratio | 1.0 |
| Memtable | SkipList |
| Bloom de memtable | Desactivado |
| Filtrado | Clave completa |
| Semilla | 20260724 |
| Estadísticas | Activadas |
| `stats_level` | 3 |
| `perf_level` | 2 |
| Histogramas | Activados |

---

## 7. Cargas y mediciones

### 7.1 Cargas

| Carga | Función |
|---|---|
| `fillrandom` | Inserta un millón de registros en orden aleatorio. |
| `compact` | Ejecuta una compactación completa. |
| `readmissing` | Consulta un millón de claves inexistentes. |
| `readrandom` | Ejecuta un millón de consultas aleatorias. |
| `stats,levelstats,memstats,sstables` | Obtiene información interna y estructural. |

### 7.2 Recursos medidos

Cada operación se ejecutó mediante:

```bash
/usr/bin/time -v
```

Se registraron:

- Tiempo de usuario.
- Tiempo de sistema.
- Tiempo real.
- Porcentaje de CPU.
- Memoria residente máxima.
- Page faults.
- Cambios de contexto.
- Entradas y salidas del sistema de archivos.

El tamaño final se obtuvo mediante:

```bash
du -sb
```

---

# 8. Resultados por motor

## 8.1 RocksDB 8.6.7

| Métrica | Bloom 10 | Ribbon 10 equivalente | Variación de Ribbon |
|---|---:|---:|---:|
| `fillrandom` | 11.828 µs/op | 11.843 µs/op | +0.13 % |
| Throughput de escritura | 84,541 ops/s | 84,438 ops/s | -0.12 % |
| Throughput de escritura | 83.8 MB/s | 83.7 MB/s | -0.12 % |
| `compact` | 1.081 s | 1.123 s | +3.87 % |
| `readmissing` | 0.356 µs/op | 0.351 µs/op | -1.40 % |
| Throughput de `readmissing` | 2,809,699 ops/s | 2,850,155 ops/s | +1.44 % |
| `readrandom` | 3.021 µs/op | 2.818 µs/op | -6.72 % |
| Throughput de `readrandom` | 331,000 ops/s | 354,823 ops/s | +7.20 % |
| Claves encontradas en `readrandom` | 1,000,000 / 1,000,000 | 1,000,000 / 1,000,000 | Sin cambio |
| Tamaño de la base | 636.46 MiB | 636.22 MiB | -0.039 % |
| FPR observado en `readmissing` | 0.970018 % | 0.941615 % | -0.028403 puntos porcentuales |

### Contadores internos

| Indicador de `readmissing` | Bloom 10 | Ribbon 10 equivalente | Variación |
|---|---:|---:|---:|
| Negativos útiles | 990,281 | 990,568 | +0.029 % |
| Positivos del filtro | 9,700 | 9,416 | -2.93 % |
| Positivos verdaderos | 0 | 0 | Sin cambio |
| Comparaciones de claves | 4,666,062 | 3,499,819 | -24.99 % |
| Bloques leídos | 7,507 | 7,292 | -2.86 % |
| Bytes de bloques procesados | 31,495,512 | 30,594,383 | -2.86 % |

### Memoria residente máxima

| Operación | RAM Bloom | RAM Ribbon | Variación de Ribbon |
|---|---:|---:|---:|
| `fillrandom` | 37.27 MiB | 34.92 MiB | -6.30 % |
| `compact` | 21.64 MiB | 24.06 MiB | +11.17 % |
| `readmissing` | 46.86 MiB | 45.99 MiB | -1.85 % |
| `readrandom` | 48.21 MiB | 48.15 MiB | -0.13 % |

En RocksDB, Ribbon mostró un comportamiento prácticamente equivalente a Bloom durante la construcción. `fillrandom` aumentó solo 0.13 % y el throughput se redujo 0.12 %, diferencias demasiado pequeñas para considerarlas concluyentes con una única ejecución. La compactación fue 3.87 % más lenta con Ribbon según el temporizador interno, mientras el tiempo real aumentó de 1.14 a 1.18 segundos. La RAM de compactación creció 11.17 %, aunque el consumo absoluto continuó siendo bajo.

En `readmissing`, Ribbon fue ligeramente mejor: la latencia disminuyó de 0.356 a 0.351 µs/op y el throughput aumentó 1.44 %. También produjo menos falsos positivos, con una FPR de 0.941615 % frente a 0.970018 % de Bloom. Los positivos del filtro disminuyeron 2.93 %, las comparaciones de claves 24.99 % y los bloques leídos 2.86 %. Esto indica que Ribbon mantuvo la precisión objetivo e incluso redujo ligeramente el trabajo interno bajo esta carga.

La diferencia más visible apareció en `readrandom`, donde las dos condiciones encontraron el 100 % de las claves. Ribbon redujo la latencia 6.72 % y elevó el throughput 7.20 %. El tamaño total de la base disminuyó únicamente 0.039 %, alrededor de 0.25 MiB. Este ahorro es pequeño porque el directorio incluye datos de 1,024 bytes por registro y otros metadatos; por ello, el tamaño total no permite medir de forma aislada el ahorro real del bloque de filtro.

---

## 8.2 Speedb 2.8.0

| Métrica | Bloom 10 | Ribbon 10 equivalente | Variación de Ribbon |
|---|---:|---:|---:|
| `fillrandom` | 4.106 µs/op | 4.245 µs/op | +3.39 % |
| Throughput de escritura | 243,539 ops/s | 235,590 ops/s | -3.26 % |
| Throughput de escritura | 241.5 MB/s | 233.7 MB/s | -3.23 % |
| `compact` | 1.911 s | 1.828 s | -4.31 % |
| `readmissing` | 0.347 µs/op | 0.373 µs/op | +7.49 % |
| Throughput de `readmissing` | 2,884,246 ops/s | 2,681,648 ops/s | -7.02 % |
| `readrandom` | 2.669 µs/op | 2.474 µs/op | -7.31 % |
| Throughput de `readrandom` | 374,672 ops/s | 404,197 ops/s | +7.88 % |
| Claves encontradas en `readrandom` | 815,813 / 1,000,000 | 815,813 / 1,000,000 | Sin cambio |
| Tamaño de la base | 636.62 MiB | 636.35 MiB | -0.043 % |
| FPR observado en `readmissing` | 0.969610 % | 0.951816 % | -0.017794 puntos porcentuales |

### Contadores internos

| Indicador de `readmissing` | Bloom 10 | Ribbon 10 equivalente | Variación |
|---|---:|---:|---:|
| Negativos útiles | 990,294 | 990,465 | +0.017 % |
| Positivos del filtro | 9,696 | 9,518 | -1.84 % |
| Positivos verdaderos | 0 | 0 | Sin cambio |
| Comparaciones de claves | 3,811,112 | 3,638,904 | -4.52 % |
| Bloques leídos | 7,225 | 7,025 | -2.77 % |
| Bytes de bloques procesados | 30,314,022 | 29,468,262 | -2.79 % |

### Memoria residente máxima

| Operación | RAM Bloom | RAM Ribbon | Variación de Ribbon |
|---|---:|---:|---:|
| `fillrandom` | 51.68 MiB | 59.77 MiB | +15.67 % |
| `compact` | 25.74 MiB | 22.71 MiB | -11.79 % |
| `readmissing` | 46.50 MiB | 45.75 MiB | -1.61 % |
| `readrandom` | 48.61 MiB | 48.71 MiB | +0.21 % |

En Speedb, Ribbon introdujo una sobrecarga más clara durante `fillrandom`. La latencia aumentó 3.39 %, el throughput en operaciones disminuyó 3.26 % y el throughput de datos cayó 3.23 %. La RAM máxima de escritura aumentó 15.67 %, de 51.68 a 59.77 MiB. Este comportamiento es consistente con un mayor costo temporal de construcción del filtro.

Durante la compactación, Ribbon fue 4.31 % más rápido según `db_bench`, mientras el tiempo real disminuyó de 1.97 a 1.87 segundos. También utilizó 11.79 % menos RAM y produjo menos salidas del sistema de archivos. En `readmissing`, sin embargo, Ribbon fue 7.49 % más lento y su throughput disminuyó 7.02 %. Aunque su FPR fue ligeramente inferior —0.951816 % frente a 0.969610 %—, esta pequeña mejora de precisión no se tradujo en mejor latencia en esta ejecución.

En `readrandom`, Ribbon redujo la latencia 7.31 % y aumentó el throughput 7.88 %. Las dos condiciones encontraron 815,813 claves, por lo que la carga combinó aproximadamente 81.58 % de consultas exitosas con 18.42 % negativas. Ribbon redujo ligeramente las comparaciones y mantuvo casi idéntico el número de bloques leídos. El tamaño total de la base descendió 0.043 %, alrededor de 0.27 MiB, una diferencia demasiado pequeña para representar por sí sola el ahorro interno del filtro.

---

# 9. Análisis combinado

## 9.1 Resultados completos

Las latencias están expresadas en µs/op, la compactación en segundos y el tamaño en MiB.

| Motor | Condición | `fillrandom` | `compact` | `readmissing` | `readrandom` | Encontradas | Tamaño | FPR |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| RocksDB 8.6.7 | Bloom 10 | 11.828 | 1.081 | 0.356 | 3.021 | 100.00 % | 636.46 | 0.970018 % |
| RocksDB 8.6.7 | Ribbon 10 equivalente | 11.843 | 1.123 | 0.351 | 2.818 | 100.00 % | 636.22 | 0.941615 % |
| Speedb 2.8.0 | Bloom 10 | 4.106 | 1.911 | 0.347 | 2.669 | 81.58 % | 636.62 | 0.969610 % |
| Speedb 2.8.0 | Ribbon 10 equivalente | 4.245 | 1.828 | 0.373 | 2.474 | 81.58 % | 636.35 | 0.951816 % |

## 9.2 Efecto porcentual de Ribbon respecto a Bloom

| Motor | `fillrandom` | `compact` | `readmissing` | `readrandom` | Tamaño | Diferencia de FPR |
|---|---:|---:|---:|---:|---:|---:|
| RocksDB 8.6.7 | +0.13 % | +3.87 % | -1.40 % | -6.72 % | -0.039 % | -0.028403 pp |
| Speedb 2.8.0 | +3.39 % | -4.31 % | +7.49 % | -7.31 % | -0.043 % | -0.017794 pp |

> Para latencia, compactación y tamaño, un valor negativo es favorable. La diferencia de FPR se expresa en puntos porcentuales.

---

## 9.3 Comparación entre motores

RocksDB y Speedb obtuvieron una FPR cercana a 1 % en ambas condiciones. Ribbon presentó una FPR ligeramente menor en los dos motores: una reducción absoluta de 0.028403 puntos porcentuales en RocksDB y de 0.017794 puntos porcentuales en Speedb. Esto confirma que `ribbon10eq` alcanzó una precisión comparable a Bloom 10.

El impacto sobre `readmissing` no fue uniforme. Ribbon mejoró 1.40 % en RocksDB, pero empeoró 7.49 % en Speedb. Por tanto, una menor FPR no garantiza automáticamente una menor latencia; también intervienen el costo de consulta del filtro, la organización de archivos, los bloques accedidos y la implementación concreta del motor.

En `readrandom`, Ribbon fue más rápido en ambos motores, con mejoras de 6.72 % en RocksDB y 7.31 % en Speedb. No obstante, la carga no fue idéntica entre motores: RocksDB encontró todas las claves y Speedb solo 81.58 %. La comparación válida principal es Ribbon frente a Bloom dentro de cada motor, no la latencia absoluta de RocksDB frente a Speedb.

---

# 10. Hallazgos

1. **Ribbon mantuvo una precisión equivalente o ligeramente mejor.** La FPR fue inferior a la de Bloom tanto en RocksDB como en Speedb, aunque las diferencias absolutas fueron pequeñas.

2. **El ahorro en el tamaño total de la base fue mínimo.** RocksDB redujo 0.039 % y Speedb 0.043 %. Esto no contradice el diseño de Ribbon: los filtros representan una fracción pequeña frente a aproximadamente 1 GiB de valores y metadatos.

3. **RocksDB mostró la transición más estable.** Ribbon tuvo una sobrecarga casi nula en escritura, mejoró ligeramente `readmissing` y mejoró de forma visible `readrandom`, aunque la compactación fue algo más lenta.

4. **Speedb mostró un intercambio más marcado.** Ribbon aumentó el costo de escritura y empeoró `readmissing`, pero mejoró la compactación y `readrandom`.

5. **Una FPR menor no implica necesariamente más velocidad.** Speedb Ribbon tuvo menos falsos positivos, pero `readmissing` fue más lento. El tiempo de consulta del filtro y la implementación del motor también son determinantes.

6. **Ribbon redujo el trabajo interno en `readmissing`.** En ambos motores disminuyeron los positivos, las comparaciones de claves y los bloques leídos.

7. **La memoria temporal dependió de la operación.** En RocksDB Ribbon redujo RAM durante escritura y lectura, pero aumentó durante compactación. En Speedb aumentó durante escritura y disminuyó durante compactación y `readmissing`.

8. **El resultado de `readrandom` no es directamente comparable entre motores.** RocksDB encontró 100 % de las claves y Speedb 81.58 %.

9. **Las entradas del sistema de archivos fueron cero en todas las lecturas.** Esto indica que el sistema operativo y las cachés influyeron fuertemente en las pruebas; los resultados representan principalmente un escenario de caché caliente.

---

# 11. Conclusión de la etapa B

La etapa B muestra que Ribbon Filter puede sustituir a Bloom Filter manteniendo una tasa de falsos positivos comparable y reduciendo ligeramente el tamaño total de la base. Sin embargo, su efecto sobre el rendimiento depende del motor y de la operación. En RocksDB, Ribbon produjo resultados favorables o casi neutros en la mayoría de las cargas, con una mejora destacable en `readrandom` y una pequeña penalización durante la compactación.

En Speedb, el comportamiento fue más mixto. Ribbon aumentó el costo de escritura y empeoró `readmissing`, pero redujo el tiempo de compactación y mejoró `readrandom`. Esto sugiere que la elección entre Bloom y Ribbon no debe hacerse únicamente por la FPR o el espacio, sino también según la proporción de escrituras, compactaciones, consultas negativas y consultas exitosas de la carga real.

En conjunto, RocksDB presentó la relación más equilibrada para Ribbon en esta ejecución. Speedb mostró beneficios específicos, pero también costos más visibles. Los resultados deben considerarse preliminares porque corresponden a una sola ejecución por condición y el ahorro del filtro se midió indirectamente mediante el tamaño completo del directorio.

---

# 12. Limitaciones

- Se realizó una sola ejecución por condición.
- No se calcularon desviaciones estándar ni intervalos de confianza.
- La política Ribbon puede combinar Bloom en archivos de flush y Ribbon en archivos compactados.
- El tamaño total del directorio no aísla el tamaño físico de los filtros.
- Las lecturas registraron cero entradas del sistema de archivos, lo que sugiere caché caliente.
- `readrandom` tuvo distinta proporción de claves encontradas entre RocksDB y Speedb.
- El orden de ejecución puede introducir efectos de temperatura y estado de caché.
- El modelo exacto del SSD M.2 no quedó registrado.
- No se evaluó Ribbon bajo distintos valores equivalentes de bits por clave.
- Speedb se evaluó con sus optimizaciones específicas desactivadas para mantener una comparación controlada.

---

# 13. Próximos pasos recomendados

1. Ejecutar al menos cinco repeticiones por condición.
2. Alternar el orden Bloom–Ribbon y Ribbon–Bloom.
3. Separar escenarios de caché caliente y caché fría.
4. Registrar el tamaño exacto de los bloques de filtro, no solo el directorio completo.
5. Medir construcción de filtros por nivel y por SSTable.
6. Repetir con valores más pequeños, donde el filtro represente una mayor proporción del almacenamiento.
7. Evaluar diferentes equivalencias de bits por clave.
8. Ejecutar una carga `readrandom` con 100 % de claves existentes en ambos motores.
9. Realizar una etapa adicional con las optimizaciones nativas de Speedb habilitadas.

---

# 14. Archivos fuente del análisis

```text
rocksdb_bloom_vs_ribbon_summary.txt
rocksdb_bloom_vs_ribbon_resources_summary.txt
rocksdb_bloom_vs_ribbon_fpr_summary.txt
speedb_bloom_vs_ribbon_summary.txt
speedb_bloom_vs_ribbon_resources_summary.txt
speedb_bloom_vs_ribbon_fpr_summary.txt
```
