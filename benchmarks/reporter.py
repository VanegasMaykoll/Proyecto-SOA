#!/usr/bin/env python3
import os
import sys
import re
import datetime
import subprocess

def get_cpu_info():
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if "model name" in line:
                    return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return "Unknown CPU"

def get_dir_size(path):
    if not path or not os.path.exists(path):
        return 0, "0 B"
    total_bytes = 0
    for dirpath, dirnames, filenames in os.walk(path):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            try:
                total_bytes += os.path.getsize(fp)
            except Exception:
                pass
    if total_bytes < 1024:
        return total_bytes, f"{total_bytes} B"
    elif total_bytes < 1024 * 1024:
        return total_bytes, f"{total_bytes / 1024:.2f} KB"
    elif total_bytes < 1024 * 1024 * 1024:
        return total_bytes, f"{total_bytes / (1024 * 1024):.2f} MB"
    else:
        return total_bytes, f"{total_bytes / (1024 * 1024 * 1024):.2f} GB"

def parse_log(log_path, target_op):
    if not os.path.exists(log_path):
        return None

    p_rocks = re.compile(r'(\w+)\s*:\s*([\d\.]+)\s*micros/op\s+([\d\.]+)\s*ops/sec\s+[\d\.]+\s*seconds\s+\d+\s+operations;?(?:\s*([\d\.]+)\s*MB/s)?(?:\s*\((\d+)\s+of\s+(\d+)\s+found\))?')
    p_level = re.compile(r'(\w+)\s*:\s*([\d\.]+)\s*micros/op;?(?:\s*([\d\.]+)\s*MB/s)?(?:\s*\((\d+)\s+of\s+(\d+)\s+found\))?')

    results = {}
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            m = p_rocks.search(line)
            if m:
                op, u_op, ops_s, mb_s, fnd, tot = m.groups()
                results[op] = {
                    "micros_op": float(u_op),
                    "ops_sec": int(float(ops_s)),
                    "mb_s": float(mb_s) if mb_s else None,
                    "found": int(fnd) if fnd is not None else None,
                    "total": int(tot) if tot is not None else None,
                }
            else:
                m2 = p_level.search(line)
                if m2:
                    op, u_op, mb_s, fnd, tot = m2.groups()
                    u_float = float(u_op)
                    ops_s = int(round(1000000.0 / u_float)) if u_float > 0 else 0
                    results[op] = {
                        "micros_op": u_float,
                        "ops_sec": ops_s,
                        "mb_s": float(mb_s) if mb_s else None,
                        "found": int(fnd) if fnd is not None else None,
                        "total": int(tot) if tot is not None else None,
                    }

    if target_op in results:
        return results[target_op]
    # Fallback: if target_op is in line
    for op, data in results.items():
        if target_op.lower() in op.lower():
            return data
    return None

def init_engine(engine_name, summary_file, params_str=""):
    engine_titles = {
        "leveldb": "LEVELDB 1.23",
        "pebblesdb": "PEBBLESDB v1.0",
        "rocksdb": "ROCKSDB 8.6.7",
        "speedb": "SPEEDB 2.8.0"
    }
    title = engine_titles.get(engine_name.lower(), engine_name.upper())

    if os.path.exists(summary_file) and os.path.getsize(summary_file) > 0:
        # Check if already initialized
        with open(summary_file, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()
            if title in content:
                return

    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    cpu = get_cpu_info()

    header = f"""====================================================================================================
 REPORTE CONSOLIDADO DE RENDIMIENTO DE FILTROS PROBABILÍSTICOS: {title}
 Fecha: {now} | CPU: {cpu}
 Parámetros Base: {params_str if params_str else 'Por defecto según configuración'}
====================================================================================================
"""
    with open(summary_file, "a", encoding="utf-8") as f:
        f.write(header)

def start_test(summary_file, test_id, test_title, test_desc=""):
    section = f"""
----------------------------------------------------------------------------------------------------
[PRUEBA {test_id}: {test_title.upper()}]
Descripción: {test_desc}
----------------------------------------------------------------------------------------------------
{('Variante'):<12} {('Filtro'):<18} {('Ops/sec'):>12} {('Latencia'):>14} {('Throughput'):>12} {('Aciertos/Total'):>18} {('Tamaño DB'):>12}
----------------------------------------------------------------------------------------------------
"""
    with open(summary_file, "a", encoding="utf-8") as f:
        f.write(section)

def add_row(summary_file, variant, log_file, target_op, db_dir=None):
    variant_labels = {
        "nofilter": "Sin Filtro",
        "bloom": "Bloom (10 bpk)",
        "ribbon": "Ribbon (10 bpk)",
        "xor": "Xor (10 bpk)",
    }
    filter_desc = variant_labels.get(variant, variant)
    data = parse_log(log_file, target_op)

    _, size_str = get_dir_size(db_dir) if db_dir else (0, "-")

    if not data:
        row = f"{variant:<12} {filter_desc:<18} {'ERROR / N/A':>12} {'-':>14} {'-':>12} {'-':>18} {size_str:>12}\n"
    else:
        ops_str = f"{data['ops_sec']:,}"
        lat_str = f"{data['micros_op']:.3f} µs/op"
        mb_str = f"{data['mb_s']:.1f} MB/s" if data['mb_s'] is not None else "-"
        found_str = f"{data['found']} / {data['total']}" if data['found'] is not None and data['total'] is not None else "-"
        row = f"{variant:<12} {filter_desc:<18} {ops_str:>12} {lat_str:>14} {mb_str:>12} {found_str:>18} {size_str:>12}\n"

    with open(summary_file, "a", encoding="utf-8") as f:
        f.write(row)

def end_test(summary_file):
    footer = "----------------------------------------------------------------------------------------------------\n"
    with open(summary_file, "a", encoding="utf-8") as f:
        f.write(footer)

def main():
    if len(sys.argv) < 2:
        print("Usage: reporter.py <command> [args...]")
        sys.exit(1)

    cmd = sys.argv[1]
    if cmd == "init-engine":
        # reporter.py init-engine <engine> <summary_file> [params_str]
        engine = sys.argv[2]
        summary_file = sys.argv[3]
        params_str = sys.argv[4] if len(sys.argv) > 4 else ""
        init_engine(engine, summary_file, params_str)

    elif cmd == "start-test":
        # reporter.py start-test <summary_file> <test_id> <test_title> [test_desc]
        summary_file = sys.argv[2]
        test_id = sys.argv[3]
        test_title = sys.argv[4]
        test_desc = sys.argv[5] if len(sys.argv) > 5 else ""
        start_test(summary_file, test_id, test_title, test_desc)

    elif cmd == "add-row":
        # reporter.py add-row <summary_file> <variant> <log_file> <target_op> [db_dir]
        summary_file = sys.argv[2]
        variant = sys.argv[3]
        log_file = sys.argv[4]
        target_op = sys.argv[5]
        db_dir = sys.argv[6] if len(sys.argv) > 6 else None
        add_row(summary_file, variant, log_file, target_op, db_dir)

    elif cmd == "end-test":
        # reporter.py end-test <summary_file>
        summary_file = sys.argv[2]
        end_test(summary_file)

    else:
        print(f"Unknown command: {cmd}")
        sys.exit(1)

if __name__ == "__main__":
    main()
