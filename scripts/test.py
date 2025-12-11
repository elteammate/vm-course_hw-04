from math import expm1

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
        result = common.temp_dir / "result.txt"
        (common.temp_dir / "input.txt").write_text(test.input_)
        with open(result, "w") as out:
            try:
                subprocess.run([
                    "cmake-build-debug/hw",
                    result_bytecode.absolute(),
                ], stdout=out, input=test.input_.encode()).check_returncode()
            except Exception as e:
                print("\n\n" + result.read_text() + "\n\n")
                raise

        if test.result is not None:
            result_lines = result.read_text().strip().splitlines()
            ref_lines = test.result.strip().splitlines()
            result_lines = [" ".join(line.split()) for line in result_lines]
            ref_lines = [" ".join(line.split()) for line in ref_lines]
            if result_lines != ref_lines:
                print(f"Test {test} result mismatch: {result_lines} {ref_lines}")
                for line in difflib.unified_diff(result.read_text().splitlines(), test.result.splitlines()):
                    print(line)
                break

if __name__ == "__main__":
    common.ensure_compiled()
    print('\n\n\n\n')
    main()
