#!/usr/bin/env python3
"""
AFL wrapper to fuzz both a bytecode file and stdin simultaneously.

File format (what AFL will mutate and supply as @@):
  [4 bytes little-endian unsigned int] N
  [N bytes]                         bytecode file contents
  [remaining bytes]                 data fed to program's stdin

Usage (wrap the VM run):
  afl-fuzz -i combined_corpus -o afl_out -- python3 afl_wrapper.py run /path/to/vm_course_02

Utility to build combined seeds from separate bytecode and stdin seeds:
  python3 afl_wrapper.py pack --bytecode bytecode_dir --stdin stdin_dir --out combined_corpus
"""

from pathlib import Path
import argparse
import struct
import subprocess
import sys
import tempfile
import shutil

def run_wrapper(vm_path: Path, combined_path: Path):
    data = combined_path.read_bytes()
    if len(data) < 4:
        # treat whole file as bytecode, no stdin
        n = len(data)
        bytecode = data
        stdin_bytes = b""
    else:
        n = struct.unpack_from("<I", data, 0)[0]
        if 4 + n <= len(data):
            bytecode = data[4:4+n]
            stdin_bytes = data[4+n:]
        else:
            # corrupted header. fallback: whole file is bytecode
            bytecode = data
            stdin_bytes = b""

    # write bytecode to temp file
    with tempfile.NamedTemporaryFile(delete=False) as tf:
        tf.write(bytecode)
        tf.flush()
        bc_path = Path(tf.name)

    # run vm with bytecode path as argument, feed stdin_bytes
    try:
        proc = subprocess.run([str(vm_path), str(bc_path)], input=stdin_bytes)
        return_code = proc.returncode
    except Exception as e:
        # cleanup and re-raise
        try:
            bc_path.unlink()
        except Exception:
            pass
        raise
    finally:
        # ensure temp file removed
        try:
            bc_path.unlink()
        except Exception:
            pass

    # propagate exit code
    sys.exit(return_code)

def pack_corpus(bytecode_dir: Path, stdin_dir: Path, out_dir: Path):
    out_dir.mkdir(parents=True, exist_ok=True)
    bc_files = sorted([p for p in bytecode_dir.iterdir() if p.is_file()])
    stdin_files = sorted([p for p in stdin_dir.iterdir() if p.is_file()])

    if not bc_files:
        raise SystemExit("No bytecode files found in " + str(bytecode_dir))
    if not stdin_files:
        raise SystemExit("No stdin files found in " + str(stdin_dir))

    count = 0
    for bc in bc_files:
        bc_bytes = bc.read_bytes()
        for st in stdin_files:
            st_bytes = st.read_bytes()
            # header + bc + stdin
            packed = struct.pack("<I", len(bc_bytes)) + bc_bytes + st_bytes
            out_name = f"pack_{bc.stem}__{st.stem}"
            out_path = out_dir / out_name
            out_path.write_bytes(packed)
            count += 1
    print(f"Packed {count} combined seeds into {out_dir.resolve()}")

def main():
    p = argparse.ArgumentParser(prog="afl_wrapper.py")
    sub = p.add_subparsers(dest="cmd", required=True)

    run = sub.add_parser("run", help="run VM using combined file supplied as @@ by AFL")
    run.add_argument("vm", type=Path, help="path to VM binary (first arg passed to wrapper)")
    run.add_argument("combined", type=Path, nargs="?", default=Path(sys.argv[0]) , help="combined input file (AFL will substitute @@)")

    pack = sub.add_parser("pack", help="pack bytecode and stdin seeds into combined files")
    pack.add_argument("--bytecode", "-b", type=Path, required=True, help="dir with bytecode seed files")
    pack.add_argument("--stdin", "-s", type=Path, required=True, help="dir with stdin seed files")
    pack.add_argument("--out", "-o", type=Path, required=True, help="output dir for combined seeds")

    args = p.parse_args()

    if args.cmd == "run":
        # AFL will call this as: python3 afl_wrapper.py run /path/to/vm @@
        # but AFL replaces @@ with a filename. argparse will provide that path in args.combined.
        run_wrapper(args.vm, args.combined)
    elif args.cmd == "pack":
        pack_corpus(args.bytecode, args.stdin, args.out)

if __name__ == "__main__":
    main()
