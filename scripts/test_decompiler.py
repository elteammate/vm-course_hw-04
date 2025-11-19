import tqdm
import difflib

import common

import subprocess


def main():
    tests = common.gather()
    for test in tqdm.tqdm(tests):
        tqdm.tqdm.write(f"{test.source}")
        test = test.read()
        result_bytecode = common.generate_bytecode(test.source)
        reference = common.temp_dir / "reference.txt"
        decompiled = common.temp_dir / "decompiled.txt"
        with open(decompiled, "w") as out:
            subprocess.run([
                "cmake-build-debug/vm_course_02",
                "decompile",
                result_bytecode.absolute(),
            ], stdout=out).check_returncode()
        with open(reference, "w") as out:
            subprocess.run([
                "./byterun.out",
                result_bytecode.absolute()
            ], stdout=out).check_returncode()

        lines_reference = [line for line in reference.read_text().splitlines() if "FAIL" not in line]
        lines_decompiled = [line for line in decompiled.read_text().splitlines() if "FAIL" not in line]
        if lines_reference != lines_decompiled:
            print(f"Test {test} result mismatch:")
            for line in difflib.unified_diff(lines_reference, lines_decompiled):
                print(line)
            break


if __name__ == "__main__":
    common.ensure_compiled()
    main()
