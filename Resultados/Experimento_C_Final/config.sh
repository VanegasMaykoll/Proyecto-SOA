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
