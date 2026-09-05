# Etapa A — Evaluación de Bloom Filter en motores clave–valor

## 1. Propósito

La etapa A evaluó el efecto de incorporar un **Bloom Filter de 10 bits por clave** en cuatro motores de almacenamiento clave–valor:

- LevelDB 1.23
- PebblesDB v1.0
- RocksDB 8.6.7
- Speedb 2.8.0

Cada motor se ejecutó bajo dos condiciones:

```text
Sin filtro probabilístico
Bloom Filter con 10 bits por clave
```

El análisis se concentró en el costo de escritura y construcción de la base, la compactación, las consultas de claves inexistentes, las consultas aleatorias, el uso de memoria y el tamaño final de la base.

> Los porcentajes de variación se calculan tomando la condición sin filtro como referencia. En las métricas de latencia, un porcentaje negativo representa una mejora.

---

## 2. Equipo utilizado

| Recurso | Especificación |
|---|---|
| Fabricante / plataforma | ASUS, identificada por Ubuntu como `ASUS System Product Name` |
| Procesador | Intel Core i5-12600KF de 12.ª generación |
| Procesadores lógicos reportados | 16 |
| Memoria RAM | 32.0 GiB |
| Almacenamiento | SSD M.2 de 2 TB |
| Gráficos | GPU NVIDIA; el modelo exacto no fue identificado en la captura |
| Sistema operativo | Ubuntu 22.04.5 LTS |
| Arquitectura | 64 bits |
| Entorno de escritorio | GNOME 42.9 |
| Sistema de ventanas | X11 |
| Caché de CPU reportada por `db_bench` | 20,480 KB |

El almacenamiento se describe como **M.2 de 2 TB** de acuerdo con la información proporcionada para el equipo. No se dispone en este registro del modelo del dispositivo ni de confirmación sobre su interfaz SATA o NVMe.

---

## 3. Versiones de los motores

| Motor | Versión | Commit |
|---|---|---|
| LevelDB | 1.23 | `99b3c03b3284f5886f9ef9a4ef703d57373e61be` |
| PebblesDB | v1.0 | `09c706d7aa2977c1316b2d64b81f1f1b6508002d` |
| RocksDB | v8.6.7 | `cb7a5e02edeb883193eb5b4901d5943f58e9add9` |
| Speedb | 2.8.0 | `c328f0ebc33db0166cd2a2844ff44c8034e30e5b` |

---

## 4. Parámetros de las pruebas

| Parámetro | Valor |
|---|---:|
| Cantidad de registros | 1,000,000 |
| Cantidad de lecturas | 1,000,000 |
| Tamaño del valor | 1,024 bytes |
| Hilos solicitados a `db_bench` | 1 |
| Block cache | 32 MiB |
| Write buffer / memtable | 4 MiB |
| Bloom Filter | 10 bits por clave |
| Compression ratio | 1.0 |
| Secuencia | `fillrandom → compact → readmissing → readrandom` |
| Base de datos | Nueva e independiente para cada condición |
| Medición de recursos | `/usr/bin/time -v` |
| Tamaño en disco | `du -sb` |
| Semilla explícita | `20260724` en RocksDB y Speedb |

### Configuración específica

- LevelDB y PebblesDB utilizaron su representación de memtable basada en SkipList.
- RocksDB y Speedb se fijaron explícitamente con `--memtablerep=skip_list`.
- RocksDB y Speedb utilizaron Snappy de forma explícita.
- En Speedb se desactivaron `enable_speedb_features` y `use_spdb_writes` para evitar que otras optimizaciones propias alteraran la comparación.
- En RocksDB y Speedb se desactivó el Bloom de memtable mediante `--memtable_bloom_size_ratio=0`.
- En LevelDB y PebblesDB la condición sin filtro utilizó `bloom_bits=-1`; en RocksDB y Speedb utilizó `bloom_bits=0`.

### Cargas ejecutadas

| Carga | Función |
|---|---|
| `fillrandom` | Inserta un millón de registros en orden aleatorio. |
| `compact` | Ejecuta una compactación completa de la base creada. |
| `readmissing` | Consulta un millón de claves inexistentes. |
| `readrandom` | Ejecuta un millón de consultas aleatorias sobre el espacio de claves. |

---

# 5. Resultados por motor

## 5.1 LevelDB 1.23

| Métrica | Sin filtro | Bloom 10 | Variación con Bloom |
|---|---:|---:|---:|
| `fillrandom` | 21.965 µs/op | 22.641 µs/op | +3.08 % |
| Throughput de escritura | 45.2 MB/s | 43.8 MB/s | -3.10 % |
| `compact` | 5.653 s | 5.306 s | -6.14 % |
| `readmissing` | 0.976 µs/op | 0.580 µs/op | -40.57 % |
| `readrandom` | 1.093 µs/op | 1.187 µs/op | +8.60 % |
| Claves encontradas en `readrandom` | 1,000,000 / 1,000,000 (100 %) | 1,000,000 / 1,000,000 (100 %) | Sin cambio |
| Tamaño de la base | N/D | N/D | N/D |
| RAM máxima en `readmissing` | 643.49 MiB | 48.97 MiB | -92.39 % |

En LevelDB, el beneficio principal de Bloom se observó en `readmissing`. La latencia bajó de 0.976 a 0.580 µs/op, una mejora de 40.57 %, y las comparaciones internas se redujeron de 23,868,025 a 20,383,248, equivalentes a una disminución de 14.60 %. El filtro permitió descartar anticipadamente búsquedas que no podían encontrar una clave en las SSTables.

La memoria residente máxima de `readmissing` disminuyó de aproximadamente 643.49 MiB a 48.97 MiB. Esta diferencia sugiere que Bloom evitó que LevelDB recorriera o cargara una cantidad considerable de páginas asociadas a SSTables. Sin embargo, las entradas del sistema de archivos fueron cero en ambas condiciones, por lo que el resultado representa principalmente un escenario atendido desde las cachés del sistema y no necesariamente desde acceso físico al SSD.

Bloom introdujo una sobrecarga de 3.08 % en `fillrandom` y de 8.60 % en `readrandom`. La compactación fue 6.14 % más rápida con Bloom, pero esta diferencia debe tratarse como variabilidad preliminar porque solo se dispone de una ejecución por condición. El tamaño final de las bases no estuvo incluido en el resumen de LevelDB analizado, por lo que se reporta como no disponible.

---

## 5.2 PebblesDB v1.0

| Métrica | Sin filtro | Bloom 10 | Variación con Bloom |
|---|---:|---:|---:|
| `fillrandom` | 8.068 µs/op | 8.568 µs/op | +6.20 % |
| Throughput de escritura | 122.9 MB/s | 115.8 MB/s | -5.78 % |
| `compact` | 2.993 s | 3.856 s | +28.82 % |
| `readmissing` | 0.386 µs/op | 0.189 µs/op | -51.04 % |
| `readrandom` | 0.386 µs/op | 0.390 µs/op | +1.04 % |
| Claves encontradas en `readrandom` | 122,791 / 1,000,000 (12.28 %) | 122,791 / 1,000,000 (12.28 %) | Sin cambio |
| Tamaño de la base | 634.36 MiB | 636.95 MiB | +0.41 % |
| RAM máxima en `readmissing` | 44.30 MiB | 59.74 MiB | +34.85 % |

PebblesDB mostró una mejora de 51.04 % en `readmissing`, al pasar de 0.386 a 0.189 µs/op. El throughput estimado de esta carga aumentó aproximadamente de 2.59 a 5.29 millones de operaciones por segundo. Este resultado confirma que Bloom fue especialmente útil cuando las consultas buscaban claves inexistentes.

El beneficio tuvo costos más visibles que en otros motores. `fillrandom` fue 6.20 % más lento, la compactación aumentó 28.82 %, el tamaño de la base creció 0.41 % y la RAM máxima de `readmissing` aumentó 34.85 %. En este motor, la mejora en búsquedas negativas estuvo acompañada por una mayor demanda de construcción, almacenamiento y memoria.

La carga `readrandom` encontró solamente 122,791 de un millón de claves en ambas condiciones, una tasa de 12.28 %. Por ello, no representa una prueba compuesta únicamente por búsquedas exitosas. Su latencia permaneció casi igual, pero este resultado no debe compararse directamente con el de LevelDB o RocksDB, cuyos `readrandom` encontraron el 100 % de las claves.

---

## 5.3 RocksDB 8.6.7

| Métrica | Sin filtro | Bloom 10 | Variación con Bloom |
|---|---:|---:|---:|
| `fillrandom` | 11.703 µs/op | 11.857 µs/op | +1.32 % |
| Throughput de escritura | 84.7 MB/s | 83.6 MB/s | -1.30 % |
| `compact` | 1.056 s | 1.073 s | +1.55 % |
| `readmissing` | 3.422 µs/op | 0.354 µs/op | -89.66 % |
| `readrandom` | 2.960 µs/op | 2.985 µs/op | +0.84 % |
| Claves encontradas en `readrandom` | 1,000,000 / 1,000,000 (100 %) | 1,000,000 / 1,000,000 (100 %) | Sin cambio |
| Tamaño de la base | 635.70 MiB | 636.46 MiB | +0.12 % |
| RAM máxima en `readmissing` | 47.26 MiB | 46.45 MiB | -1.71 % |

RocksDB presentó una de las mejoras más claras en `readmissing`. La latencia descendió de 3.422 a 0.354 µs/op, una reducción de 89.66 %, mientras que el throughput pasó de 292,220 a 2,824,627 operaciones por segundo. Bloom registró 990,281 negativos útiles, 9,700 positivos y cero positivos verdaderos, lo que corresponde a una tasa observada de falsos positivos cercana a 0.97 %.

El número de fallos de caché de bloques de datos durante `readmissing` pasó de 1,194,407 sin filtro a 7,516 con Bloom, una reducción aproximada de 99.37 %. La RAM máxima se mantuvo estable y disminuyó ligeramente, de 47.26 a 46.45 MiB. Esto muestra que el filtro redujo de forma muy significativa el acceso innecesario a bloques sin aumentar materialmente la memoria del proceso.

Los costos en otras cargas fueron pequeños: `fillrandom` empeoró 1.32 %, la compactación 1.55 %, `readrandom` 0.84 % y el tamaño final 0.12 %. Como `readrandom` encontró el millón de claves, Bloom no pudo evitar el acceso final a los datos y añadió solamente una comprobación previa. RocksDB mostró, por tanto, una relación costo–beneficio favorable para cargas con búsquedas negativas.

---

## 5.4 Speedb 2.8.0

| Métrica | Sin filtro | Bloom 10 | Variación con Bloom |
|---|---:|---:|---:|
| `fillrandom` | 4.052 µs/op | 4.087 µs/op | +0.86 % |
| Throughput de escritura | 244.8 MB/s | 242.7 MB/s | -0.86 % |
| `compact` | 1.209 s | 1.788 s | +47.88 % |
| `readmissing` | 3.283 µs/op | 0.345 µs/op | -89.49 % |
| `readrandom` | 2.821 µs/op | 2.420 µs/op | -14.21 % |
| Claves encontradas en `readrandom` | 815,813 / 1,000,000 (81.58 %) | 815,813 / 1,000,000 (81.58 %) | Sin cambio |
| Tamaño de la base | 635.74 MiB | 636.56 MiB | +0.13 % |
| RAM máxima en `readmissing` | 47.68 MiB | 45.98 MiB | -3.56 % |

Speedb también obtuvo una mejora muy marcada en `readmissing`. La latencia disminuyó de 3.283 a 0.345 µs/op, una reducción de 89.49 %, y el throughput aumentó de 304,637 a 2,896,149 operaciones por segundo. El filtro registró 990,318 negativos útiles, 9,665 positivos y ningún positivo verdadero, para una tasa observada de falsos positivos cercana a 0.97 %.

Las comparaciones de claves de `readmissing` disminuyeron 46.12 % y los bloques leídos pasaron de 1,147,257 a 7,092, una reducción aproximada de 99.38 %. Bloom también mejoró `readrandom` en 14.21 %, pero esta carga encontró solo 815,813 claves. Las consultas negativas restantes pudieron ser descartadas por el filtro, por lo que la mejora no demuestra que Bloom acelere por sí mismo las búsquedas exitosas.

El costo de escritura fue muy pequeño: `fillrandom` aumentó 0.86 % y el tamaño de la base 0.13 %. En contraste, la compactación fue 47.87 % más lenta con Bloom. Este resultado sugiere un costo importante de construcción o reescritura del filtro durante la compactación, aunque debe confirmarse mediante repeticiones antes de considerarlo una característica estable.

---

# 6. Hallazgos comparativos

## 6.1 Todos los resultados

Las latencias están expresadas en µs/op, la compactación en segundos y el tamaño en MiB.

| Motor | Condición | `fillrandom` | `compact` | `readmissing` | `readrandom` | Encontradas en `readrandom` | Tamaño |
|---|---|---:|---:|---:|---:|---:|---:|
| LevelDB 1.23 | Sin filtro | 21.965 | 5.653 | 0.976 | 1.093 | 100 % | N/D |
| LevelDB 1.23 | Bloom 10 | 22.641 | 5.306 | 0.580 | 1.187 | 100 % | N/D |
| PebblesDB v1.0 | Sin filtro | 8.068 | 2.993 | 0.386 | 0.386 | 12.28 % | 634.36 |
| PebblesDB v1.0 | Bloom 10 | 8.568 | 3.856 | 0.189 | 0.390 | 12.28 % | 636.95 |
| RocksDB 8.6.7 | Sin filtro | 11.703 | 1.056 | 3.422 | 2.960 | 100 % | 635.70 |
| RocksDB 8.6.7 | Bloom 10 | 11.857 | 1.073 | 0.354 | 2.985 | 100 % | 636.46 |
| Speedb 2.8.0 | Sin filtro | 4.052 | 1.209 | 3.283 | 2.821 | 81.58 % | 635.74 |
| Speedb 2.8.0 | Bloom 10 | 4.087 | 1.788 | 0.345 | 2.420 | 81.58 % | 636.56 |

## 6.2 Efecto porcentual de Bloom

| Motor | `fillrandom` | `compact` | `readmissing` | `readrandom` | Tamaño | RAM en `readmissing` |
|---|---:|---:|---:|---:|---:|---:|
| LevelDB 1.23 | +3.08 % | -6.14 % | -40.57 % | +8.60 % | N/D | -92.39 % |
| PebblesDB v1.0 | +6.20 % | +28.82 % | -51.04 % | +1.04 % | +0.41 % | +34.85 % |
| RocksDB 8.6.7 | +1.32 % | +1.55 % | -89.66 % | +0.84 % | +0.12 % | -1.71 % |
| Speedb 2.8.0 | +0.86 % | +47.88 % | -89.49 % | -14.21 % | +0.13 % | -3.56 % |

> En las columnas de latencia y tiempo, un resultado negativo es favorable porque representa una reducción. En throughput, el sentido sería el opuesto.

---

## 6.3 Principales hallazgos

1. **Bloom mejoró `readmissing` en los cuatro motores.** Las reducciones de latencia fueron 40.57 % en LevelDB, 51.04 % en PebblesDB, 89.66 % en RocksDB y 89.49 % en Speedb. El beneficio fue especialmente grande en los motores derivados de RocksDB.

2. **RocksDB y Speedb mostraron una FPR cercana a 0.97 %.** Este valor es coherente con un Bloom Filter configurado con 10 bits por clave y demuestra que aproximadamente 99 % de las consultas negativas internas fueron descartadas correctamente.

3. **El costo de escritura fue moderado.** `fillrandom` empeoró entre 0.86 % y 6.20 %. RocksDB y Speedb mostraron la menor sobrecarga, mientras PebblesDB presentó la mayor.

4. **La compactación fue la métrica más variable.** LevelDB mejoró 6.14 %, RocksDB empeoró solo 1.55 %, PebblesDB empeoró 28.82 % y Speedb 47.87 %. Al existir una sola ejecución por condición, estas diferencias no permiten todavía separar el efecto real del filtro de la variabilidad temporal del sistema.

5. **El crecimiento de almacenamiento fue pequeño.** En los tres motores con medición disponible, Bloom incrementó el tamaño entre 0.12 % y 0.41 %. El costo espacial fue reducido frente a la mejora conseguida en búsquedas negativas.

6. **`readrandom` no fue equivalente entre motores.** LevelDB y RocksDB encontraron 100 % de las claves, Speedb 81.58 % y PebblesDB 12.28 %. Por tanto, la comparación directa de esta carga entre motores no es metodológicamente válida. Sí es válido comparar Bloom frente a no filtro dentro de cada motor, porque ambas condiciones tuvieron la misma tasa de claves encontradas.

7. **La comparación entre motores debe distinguir efecto relativo y rendimiento absoluto.** PebblesDB tuvo la menor latencia absoluta de `readmissing` con Bloom, pero su `readrandom` fue mayoritariamente negativo. RocksDB y Speedb proporcionan la comparación interna más completa gracias a sus contadores de filtros y bloques.

---

# 7. Conclusión de la etapa A

Los resultados preliminares confirman que Bloom Filter es especialmente beneficioso en cargas con consultas negativas. En todos los motores redujo la latencia de `readmissing`, pero el efecto fue mucho más pronunciado en RocksDB y Speedb, donde se evitó más de 99 % de las lecturas de bloques que se producían sin filtro. La sobrecarga en escritura y almacenamiento fue generalmente pequeña, aunque PebblesDB mostró un costo superior y Speedb presentó un aumento considerable en el tiempo de compactación.

No se observó un beneficio universal en `readrandom`. Cuando todas las claves existían, como en LevelDB y RocksDB, Bloom fue neutro o ligeramente perjudicial. Cuando la carga contenía búsquedas negativas, como en Speedb, el filtro sí mejoró el resultado global. Esto confirma que la utilidad de Bloom depende de la proporción de consultas inexistentes y no solamente del motor utilizado.

En términos generales, RocksDB presentó la relación más equilibrada entre costo y beneficio: una mejora cercana al 90 % en `readmissing`, una FPR de aproximadamente 0.97 % y sobrecargas inferiores a 2 % en escritura, compactación, consultas exitosas y almacenamiento. Speedb consiguió una mejora similar en búsquedas negativas y el mejor throughput de escritura absoluto, pero su compactación con Bloom fue notablemente más costosa. LevelDB y PebblesDB también se beneficiaron, aunque con efectos más modestos o mayores costos secundarios.

---

# 8. Limitaciones

- Se dispone de una sola ejecución por condición.
- No se calcularon desviación estándar ni intervalos de confianza.
- El orden de ejecución puede influir en temperatura, caché y frecuencia del procesador.
- Las entradas del sistema de archivos fueron cero en varias lecturas, lo que indica una fuerte influencia de las cachés.
- La tasa de claves encontradas por `readrandom` no fue uniforme entre motores.
- Los comandos separados de estadísticas de RocksDB y Speedb mostraron en algunas cabeceras valores predeterminados distintos del `value_size` del benchmark; los resultados principales deben interpretarse desde los logs de cada carga.
- La medición del tamaño de LevelDB no estuvo disponible en el resumen analizado.
- El modelo exacto del SSD M.2 y de la GPU no quedó registrado.

---

# 9. Próximos pasos recomendados

Para fortalecer los resultados de la etapa A:

1. Ejecutar al menos cinco repeticiones por condición.
2. Alternar el orden de las condiciones para reducir sesgos temporales.
3. Separar pruebas con caché caliente y caché fría.
4. Usar una carga de consultas exitosas común y verificable para todos los motores.
5. Calcular media, mediana, desviación estándar e intervalos de confianza.
6. Registrar temperatura, frecuencia de CPU y utilización del sistema durante cada repetición.
