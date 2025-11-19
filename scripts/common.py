from pathlib import Path
import dataclasses
from typing import Optional, Callable
import re
import subprocess


@dataclasses.dataclass
class TestGroup:
    root: Path
    kind: str


groups = [
    TestGroup(root=Path("tests"), kind="plain"),
    TestGroup(root=Path("Lama/regression"), kind="cram"),
    TestGroup(root=Path("Lama/regression_long/expressions"), kind="cram"),
    TestGroup(root=Path("Lama/regression_long/deep-expressions"), kind="cram"),
    TestGroup(root=Path("Lama/performance"), kind="plain"),
]


temp_dir = Path("/tmp/hw-02")
temp_dir.mkdir(exist_ok=True)


@dataclasses.dataclass
class Test:
    group: TestGroup
    source: Path | str
    input_: Optional[Path | str]
    result: Optional[Path | str]

    def read(self) -> "Test":
        source = self.source
        input_ = self.input_
        result = self.result
        if isinstance(source, Path):
            source = source.read_text()
        if isinstance(input_, Path):
            input_ = input_.read_text()
        if input_ is None:
            input_ = ""
        if isinstance(result, Path):
            result = result.read_text()
        if result is None:
            result = ""
        return Test(group=self.group, source=source, input_=input_, result=result)


def _parse_cram_file(path: Path, group: TestGroup) -> list[Test]:
    text = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    tests: list[Test] = []

    cmd_re = re.compile(r"^ {2}\$\s+(.*)")
    output_re = re.compile(r"^ {2}(.*)$")

    current_source = None
    current_input = None
    current_output = []

    for line in text + [""]:
        cmd_match = cmd_re.match(line)
        if cmd_match:
            if current_source:
                tests.append(
                    Test(
                        group=group,
                        source=current_source,
                        input_=current_input,
                        result="\n".join(current_output).strip() if current_output else None,
                    )
                )
                current_output = []

            cmd = cmd_match.group(1)
            lama_match = re.search(r"(\S+\.lama)", cmd)
            input_match = re.search(r"<\s*(\S+\.input)", cmd)

            current_source = group.root / Path(lama_match.group(1)) if lama_match else None
            current_input = group.root / Path(input_match.group(1)) if input_match else None

        elif line.strip() == "":
            continue
        elif current_source:
            out_match = output_re.match(line)
            if out_match:
                current_output.append(out_match.group(1))

    if current_source:
        tests.append(
            Test(
                group=group,
                source=current_source,
                input_=current_input,
                result="\n".join(current_output).strip() if current_output else None,
            )
        )
    return tests


def gather(filter_: Callable[[Test], bool] = lambda x: True) -> list[Test]:
    result: list[Test] = []

    for group in groups:
        if not group.root.exists():
            continue

        if group.kind == "plain":
            for lama_file in sorted(group.root.rglob("*.lama")):
                test = Test(group=group, source=lama_file, input_=None, result=None)
                if filter_(test):
                    result.append(test)

        elif group.kind == "cram":
            for cram_file in sorted(group.root.rglob("*.t")):
                for test in _parse_cram_file(cram_file, group):
                    if filter_(test):
                        result.append(test)

    result = [test for test in result if test.source not in [
        Path("Lama/regression/test054.lama"),
        Path("Lama/regression/test110.lama"),
        Path("Lama/regression/test111.lama"),
        Path("Lama/regression/test803.lama"),
    ]]

    return result


def ensure_compiled():
    subprocess.run([
        "cmake",
        "--build",
        "cmake-build-debug",
        "--target",
        "vm_course_02",
    ]).check_returncode()
    subprocess.run([
        "gcc",
        "Lama/byterun/byterun.c",
        "Lama/runtime/runtime.a",
        "-I",
        "Lama/runtime",
        "-o",
        "byterun.out",
    ]).check_returncode()


def generate_bytecode(source: str | Path, output: Optional[Path] = None) -> Path:
    if isinstance(source, str):
        new_path = Path(temp_dir / "tmp.lama")
        filename = Path("tmp.lama")
        new_path.write_text(source)
        source = new_path
    else:
        filename = source.name
    subprocess.run([
        "lamac",
        "-b",
        source.absolute(),
    ], cwd=temp_dir).check_returncode()
    result_path = temp_dir / filename.with_suffix(".bc")
    if output is not None:
        result_path = result_path.rename(output)
    return result_path


if __name__ == "__main__":
    ensure_compiled()
    print(gather())
