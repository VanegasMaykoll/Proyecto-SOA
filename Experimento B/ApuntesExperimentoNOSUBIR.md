LevelDB 1.23 NO tiene Ribbon Filter nativo. A diferencia de RocksDB y SpeedDB (que son forks de RocksDB y soportan --use_ribbon_filter), LevelDB solo expone la interfaz FilterPolicy con una única implementación: BloomFilterPolicy.

Para replicar el "Experimento B" (Bloom vs filtro alternativo) en LevelDB, hay que implementar un Ribbon Filter manualmente y conectarlo a la interfaz FilterPolicy

Opción A — Implementar Ribbon Filter manualmente en LevelDB
Crear un RibbonFilterPolicy que herede de FilterPolicy en el código fuente de LevelDB
Agregar un flag --use_ribbon_filter al db_bench.cc
Recompilar LevelDB
Pro: Comparación directa Bloom vs Ribbon como en RocksDB/SpeedDB
Contra: Modifica el motor original, la implementación no sería la misma de RocksDB (distinta calidad/optimización), los resultados no serían estrictamente comparables

Si eliges la Opción A (implementar Ribbon manualmente), el trabajo es significativamente más complejo — requiere implementar el algoritmo Ribbon en C++, integrarlo con la interfaz FilterPolicy de LevelDB, agregar el flag al benchmark, y recompilar. Aproximadamente ~300-500 líneas de código C++ adicional.



Implementar RibbonFilter en C++ y conectarlo a LevelDB 1.23

Como LevelDB 1.23 no tiene Ribbon Filter nativo, necesitamos: 

Crear una clase RibbonFilter que implemente la interfaz leveldb::FilterPolicy. 
Configurar db_options.filter_policy = &ribbon_filter; 

Pasos

ribbon_filter.h

Crear la clase 
Implementar los métodos key_processor

key_processor(const Slice& key, Slice* result) const;

Agregar métodos de configuración: 

num_probes()

num_levels()

num_buckets()

num_hashes_per_probe()

Conectarlo en main.cc

Options options;
RibbonFilter ribbon_filter;
options.filter_policy = &ribbon_filter;
DB::Open(options, "/tmp/testdb", &db);