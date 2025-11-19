from pathlib import Path
import argparse
import hashlib
import struct
import common

def hash_bytes(b: bytes) -> str:
    return hashlib.sha1(b).hexdigest()

def write_unique(seen, contents: bytes, dest_dir: Path, prefix: str):
    h = hash_bytes(contents)
    if h in seen:
        return None
    seen.add(h)
    idx = len(seen)
    name = f"{prefix}_{idx:04d}_{h[:8]}"
    p = dest_dir / name
    p.write_bytes(contents)
    return p

def make_combined_seed(bc_bytes: bytes, input_bytes: bytes) -> bytes:
    length_prefix = struct.pack("<I", len(bc_bytes))
    return length_prefix + bc_bytes + input_bytes

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", "-o", default="afl_corpus", help="output corpus dir")
    parser.add_argument("--include-bytecode", action="store_true", help="also emit compiled bytecode files")
    args = parser.parse_args()

    out = Path(args.out)
    stdin_dir = out / "stdin"
    bytecode_dir = out / "bytecode"
    stdin_dir.mkdir(parents=True, exist_ok=True)
    if args.include_bytecode:
        bytecode_dir.mkdir(parents=True, exist_ok=True)

    common.ensure_compiled()

    tests = common.gather()
    seen_combined = set()
    seen_bytecode = set()

    for i, t in enumerate(tests, start=1):
        t_read = t.read()
        input_bytes = (t_read.input_ or "").encode()
        if input_bytes == b"":
            input_bytes = b"\n"

        try:
            bytecode_path = common.generate_bytecode(t_read.source)
            bc_bytes = Path(bytecode_path).read_bytes()
        except Exception:
            bc_bytes = b""

        if args.include_bytecode:
            write_unique(seen_bytecode, bc_bytes, bytecode_dir, f"bc{i}")

        combined = make_combined_seed(bc_bytes, input_bytes)
        write_unique(seen_combined, combined, stdin_dir, f"seed{i}")

    if not list(stdin_dir.iterdir()):
        combined = make_combined_seed(b"", b"\n")
        (stdin_dir / "empty_seed").write_bytes(combined)
        seen_combined.add(hash_bytes(combined))

    print(f"Done. combined stdin seeds: {len(seen_combined)}")
    if args.include_bytecode:
        print(f"bytecode seeds: {len(seen_bytecode)}")
    print(f"Corpus at: {out.resolve()}")

if __name__ == "__main__":
    main()
