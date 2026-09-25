#!/usr/bin/env python3
"""Run the autonomous YACP simulator regression suite.

The suite uses a fresh simulator SD-card directory for every scenario. It
builds the X3 simulator once, launches each scenario without user input, checks
the process output and validates the generated BMP captures.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zipfile
from dataclasses import dataclass
from datetime import date
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REFERENCE_RELEASE = "v1.7.0-yacp.3"
CJK_FONT_FAMILY = "MicrosoftYaHei"
CJK_FONT_FILENAME = "MicrosoftYaHei_12.cpfont"
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
)


@dataclass(frozen=True)
class Scenario:
    name: str
    kind: str
    source_book: str | None
    mode: str | None
    stats_demo: str | None
    markers: tuple[str, ...]
    screenshots: tuple[tuple[int, str], ...]
    timeout: int = 45


SCENARIOS = (
    Scenario(
        "reader-layout",
        "smoke",
        "test_reader_rendering_matrix.epub",
        None,
        None,
        ("Simulator smoke test passed", "Rendering Reader after page forward"),
        ((120, "reader-start.bmp"), (420, "reader-menu.bmp"), (700, "reader-final.bmp")),
    ),
    Scenario(
        "reader-br-break",
        "smoke",
        "test_br_section_break.epub",
        None,
        None,
        ("Simulator smoke test passed", "Rendering Reader after page forward"),
        ((120, "br-start.bmp"), (420, "br-final.bmp")),
    ),
    Scenario(
        "dictionary-lookup",
        "dictionary",
        None,
        "dictionary",
        None,
        (
            "Simulator smoke test passed",
            "Loaded SD font family: MicrosoftYaHei",
            "Prewarming scanned SD-card font",
            "Using prepared dictionary /.dictionaries/MyDict/Cambridge",
            "Rendering Dictionary Definition",
        ),
        ((120, "dictionary-reader.bmp"), (410, "dictionary-selector.bmp"), (550, "dictionary-definition.bmp")),
    ),
    Scenario(
        "reading-achievement",
        "stats",
        None,
        None,
        "achievement",
        ("Starting reading statistics demo: achievement",),
        ((100, "achievement.bmp"),),
    ),
    Scenario(
        "reading-rhythm",
        "stats",
        None,
        None,
        "rhythm",
        ("Starting reading statistics demo: rhythm",),
        ((100, "reading-rhythm.bmp"),),
    ),
    Scenario(
        "finished-books",
        "stats",
        None,
        None,
        "finished-books",
        ("Starting reading statistics demo: finished-books",),
        ((100, "finished-books.bmp"),),
    ),
    Scenario(
        "autonomy",
        "power",
        None,
        None,
        "power",
        ("Starting power statistics demo",),
        ((100, "autonomy.bmp"),),
    ),
    Scenario(
        "cjk-metadata-and-reader",
        "cjk",
        None,
        "cjk",
        None,
        ("Simulator smoke test passed", f"UI metadata font ready: {CJK_FONT_FAMILY}"),
        ((120, "cjk-home.bmp"), (650, "cjk-reader.bmp")),
    ),
)


def read_version() -> str:
    match = re.search(r"^crossink_version\s*=\s*(\S+)", (ROOT / "platformio.ini").read_text(encoding="utf-8"), re.MULTILINE)
    return match.group(1) if match else "unknown"


def wsl_path(path: Path) -> str:
    value = str(path.resolve())
    drive, tail = os.path.splitdrive(value)
    if not drive:
        return value.replace("\\", "/")
    return f"/mnt/{drive[0].lower()}{tail.replace('\\', '/')}"


def run_from_windows(args: argparse.Namespace) -> int:
    command = [
        "wsl.exe",
        "-d",
        "Ubuntu",
        "--",
        "python3",
        wsl_path(Path(__file__)),
        "--from-windows",
        "--artifact-root",
        wsl_path(ROOT / "qa-artifacts" / "simulator-regression"),
    ]
    if not args.build:
        command.append("--no-build")
    if args.window:
        command.append("--window")
    if args.scenario:
        command.extend(["--scenario", args.scenario])
    if args.font_source:
        command.extend(["--font-source", wsl_path(Path(args.font_source))])
    return subprocess.run(command).returncode


def stage_wsl_source(args: argparse.Namespace) -> int:
    stage_root = Path.home() / f"yacp-simulator-regression-{int(time.time())}"
    rsync = shutil.which("rsync")
    if rsync is None:
        print("rsync is required in WSL to stage the firmware outside the mounted Windows tree", file=sys.stderr)
        return 2

    excludes = (
        ".git",
        ".pio",
        ".analysis",
        "analysis",
        "build",
        "dist",
        "bin",
        "fs_",
        "qa-artifacts",
        ".venv",
        "*.bin",
    )
    command = [rsync, "-a"]
    for pattern in excludes:
        command.extend([f"--exclude={pattern}"])
    command.extend([f"{ROOT}/", f"{stage_root}/"])
    print(f"Staging simulator source in {stage_root}", flush=True)
    result = subprocess.run(command)
    if result.returncode != 0:
        return result.returncode

    font_source = args.font_source
    if not font_source:
        candidate = ROOT / "fs_" / ".fonts" / CJK_FONT_FAMILY / CJK_FONT_FILENAME
        if candidate.is_file():
            font_source = str(candidate)

    command = [
        sys.executable,
        str(stage_root / "scripts" / "run_simulator_regression.py"),
        "--internal-stage",
        "--artifact-root",
        str(args.artifact_root),
    ]
    if not args.build:
        command.append("--no-build")
    if args.window:
        command.append("--window")
    if args.scenario:
        command.extend(["--scenario", args.scenario])
    if font_source:
        command.extend(["--font-source", font_source])
    return subprocess.run(command).returncode


def build_simulator(artifact_root: Path) -> int:
    pio = shutil.which("pio") or str(Path.home() / ".platformio" / "penv" / "bin" / "pio")
    build_log = artifact_root / "build.log"
    build_log.parent.mkdir(parents=True, exist_ok=True)
    print("Building simulator_x3...", flush=True)
    result = subprocess.run(
        [pio, "run", "-e", "simulator_x3"],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    build_log.write_text(result.stdout, encoding="utf-8")
    if result.returncode != 0:
        print(result.stdout, end="", file=sys.stderr)
        print(f"Simulator build failed. Full log: {build_log}", file=sys.stderr)
        return result.returncode
    print(f"Build passed. Log: {build_log}", flush=True)
    return 0


def write_epub(path: Path, title: str, author: str, body: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    container = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""
    opf = f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="bookid" version="2.0">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:yacp-simulator-regression</dc:identifier>
    <dc:title>{title}</dc:title><dc:creator>{author}</dc:creator>
    <dc:language>en</dc:language>
  </metadata>
  <manifest><item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/></manifest>
  <spine><itemref idref="chapter"/></spine>
</package>
"""
    chapter = f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>{title}</title></head>
<body><h1>{title}</h1><p>{body}</p></body></html>
"""
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        archive.writestr("META-INF/container.xml", container)
        archive.writestr("OEBPS/content.opf", opf)
        archive.writestr("OEBPS/chapter.xhtml", chapter)


def write_dictionary(fs_root: Path) -> None:
    # Mirror the user-reported CrossInk layout and Cambridge metadata. Cambridge
    # uses XDXF (`sametypesequence=x`) and includes CrossInk's coarse .idx.oft
    # beside the .idx.oft.cspt accelerator.
    base = fs_root / ".dictionaries" / "MyDict"
    base.mkdir(parents=True, exist_ok=True)
    headword = b"autonomous"
    definition = (
        b"<k>autonomous</k><br/><def>Able to operate without external control.</def>"
        b"<br/><example>This Cambridge-compatible XDXF entry is supplied by the simulator fixture.</example>"
    )
    index = headword + b"\0" + struct.pack(">II", 0, len(definition))
    cspt = struct.pack("<4sBBHI", b"CSPT", 1, 16, 16, 1)
    cspt += headword.ljust(16, b"\0") + struct.pack("<I", 0)
    (base / "Cambridge.ifo").write_text(
        "StarDict's dict ifo file\nversion=3.0.0\nwordcount=1\n"
        f"idxfilesize={len(index)}\nbookname=Cambridge QA\nsametypesequence=x\n",
        encoding="utf-8",
    )
    (base / "Cambridge.idx").write_bytes(index)
    (base / "Cambridge.dict").write_bytes(definition)
    (base / "Cambridge.idx.oft").write_bytes(b"StarDict's oft file\nversion=2.4.2\0\0\0\0")
    (base / "Cambridge.idx.oft.cspt").write_bytes(cspt)


def write_stats(fs_root: Path) -> None:
    scripts_dir = Path(__file__).resolve().parent
    if str(scripts_dir) not in sys.path:
        sys.path.insert(0, str(scripts_dir))
    from generate_reading_stats_demo import build_finished_books, build_global_stats, day_index, demo_minutes

    anchor = date(2026, 9, 13)
    minutes = demo_minutes(anchor)
    stats_dir = fs_root / ".crosspoint"
    stats_dir.mkdir(parents=True, exist_ok=True)
    (stats_dir / "global_stats.bin").write_bytes(build_global_stats(anchor, minutes))
    (stats_dir / "daily_reading.bin").write_bytes(b"CRHM" + bytes([1]) + struct.pack("<I", day_index(anchor)) + bytes(minutes))
    (stats_dir / "finished_books.bin").write_bytes(build_finished_books())


def write_cjk_settings(fs_root: Path, book_path: str, version: str) -> None:
    settings = {
        "language": "EN",
        "uiTheme": 6,
        "fontFamily": 0,
        "fontSize": 0,
        "sdFontFamilyName": CJK_FONT_FAMILY,
        "buttonLayoutPromptVersion": version,
    }
    recent = {"books": [{"path": book_path, "title": "自動テスト", "author": "測試作者", "coverBmpPath": ""}]}
    settings_path = fs_root / ".crosspoint"
    settings_path.mkdir(parents=True, exist_ok=True)
    (settings_path / "crossink-settings.json").write_text(json.dumps(settings), encoding="utf-8")
    (settings_path / "recent.json").write_text(json.dumps(recent, ensure_ascii=False), encoding="utf-8")


def prepare_scenario(work_root: Path, scenario: Scenario, font_source: str | None) -> tuple[Path, str]:
    scenario_root = work_root / scenario.name
    fs_root = scenario_root / "fs_"
    books_root = fs_root / "books"
    books_root.mkdir(parents=True, exist_ok=True)

    if scenario.kind == "smoke":
        source = ROOT / "test" / "epubs" / str(scenario.source_book)
        target = books_root / source.name
        shutil.copy2(source, target)
    elif scenario.kind == "dictionary":
        target = books_root / "dictionary.epub"
        italic = " ".join(["autonomous"] * 40)
        regular = " ".join(["autonomous"] * 140)
        write_epub(target, "Autonomous Dictionary", "YACP QA", f"<i>{italic}</i></p><p>{regular}")
        write_dictionary(fs_root)
        source = Path(font_source) if font_source else ROOT / "fs_" / ".fonts" / CJK_FONT_FAMILY / CJK_FONT_FILENAME
        if not source.is_file():
            raise RuntimeError(f"Custom font fixture not found: {source}")
        font_target = fs_root / ".fonts" / CJK_FONT_FAMILY / CJK_FONT_FILENAME
        font_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, font_target)
        write_cjk_settings(fs_root, "/books/dictionary.epub", read_version())
    elif scenario.kind == "cjk":
        target = books_root / "cjk-metadata.epub"
        write_epub(target, "自動テスト", "測試作者", "自動テスト 測試作者 CJK metadata reader verification")
        source = Path(font_source) if font_source else ROOT / "fs_" / ".fonts" / CJK_FONT_FAMILY / CJK_FONT_FILENAME
        if not source.is_file():
            raise RuntimeError(f"CJK font fixture not found: {source}")
        font_target = fs_root / ".fonts" / CJK_FONT_FAMILY / CJK_FONT_FILENAME
        font_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, font_target)
        write_cjk_settings(fs_root, "/books/cjk-metadata.epub", read_version())
    elif scenario.kind in {"stats", "power"}:
        target = books_root / "stats-placeholder.epub"
        write_stats(fs_root)
        write_epub(target, "Statistics Fixture", "YACP QA", "Statistics screen fixture")
    else:
        raise RuntimeError(f"Unsupported scenario kind: {scenario.kind}")

    scenario_root.mkdir(parents=True, exist_ok=True)
    return scenario_root, f"/books/{target.name}"


def bmp_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("not a BMP file")
    width, height = struct.unpack_from("<ii", data, 18)
    if width <= 0 or height == 0:
        raise ValueError("invalid BMP dimensions")
    return width, abs(height)


def selected_scenarios(name: str | None) -> list[Scenario]:
    if not name:
        return list(SCENARIOS)
    selected = [scenario for scenario in SCENARIOS if scenario.name == name]
    if not selected:
        raise ValueError(f"Unknown scenario: {name}")
    return selected


def run_scenario(work_root: Path, artifact_root: Path, scenario: Scenario, font_source: str | None, window: bool) -> dict:
    started = time.time()
    result = {"name": scenario.name, "kind": scenario.kind, "status": "failed", "screenshots": [], "errors": []}
    try:
        scenario_root, book_path = prepare_scenario(work_root, scenario, font_source)
        captures = scenario_root / "captures"
        captures.mkdir(parents=True, exist_ok=True)
        screenshot_script = ";".join(f"{at}:captures/{filename}" for at, filename in scenario.screenshots)

        env = os.environ.copy()
        for key in (
            "CROSSINK_SIMULATOR_SMOKE_TEST",
            "CROSSINK_SIMULATOR_SMOKE_BOOK",
            "CROSSINK_SIMULATOR_SMOKE_MODE",
            "CROSSINK_SIMULATOR_STATS_DEMO",
            "CROSSINK_SIM_POWER_DEMO",
        ):
            env.pop(key, None)
        env["CROSSPOINT_SIM_INPUT_SCRIPT"] = "18000:QUIT"
        env["CROSSPOINT_SIM_SCREENSHOTS"] = screenshot_script
        if not window:
            env["SDL_VIDEODRIVER"] = "dummy"

        if scenario.kind in {"smoke", "dictionary", "cjk"}:
            env["CROSSINK_SIMULATOR_SMOKE_TEST"] = "1"
            env["CROSSINK_SIMULATOR_SMOKE_BOOK"] = book_path
            if scenario.mode:
                env["CROSSINK_SIMULATOR_SMOKE_MODE"] = scenario.mode
        if scenario.stats_demo:
            env["CROSSINK_SIMULATOR_STATS_DEMO"] = scenario.stats_demo
            if scenario.stats_demo == "power":
                env["CROSSINK_SIM_POWER_DEMO"] = "1"

        program = ROOT / ".pio" / "build" / "simulator_x3" / "program"
        process = subprocess.run(
            [str(program)],
            cwd=scenario_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=scenario.timeout,
        )
        output = process.stdout
        (scenario_root / "stdout.log").write_text(output, encoding="utf-8")
        if process.returncode != 0:
            result["errors"].append(f"process exit code {process.returncode}")
        for pattern in CRASH_PATTERNS:
            if pattern in output:
                result["errors"].append(f"crash pattern: {pattern}")
        for marker in scenario.markers:
            if marker not in output:
                result["errors"].append(f"missing log marker: {marker}")

        for _, filename in scenario.screenshots:
            capture = captures / filename
            if not capture.is_file():
                result["errors"].append(f"missing screenshot: {filename}")
                continue
            try:
                result["screenshots"].append({"file": filename, "size": bmp_dimensions(capture)})
            except (OSError, ValueError) as error:
                result["errors"].append(f"invalid screenshot {filename}: {error}")

        result["status"] = "passed" if not result["errors"] else "failed"
        result["exit_code"] = process.returncode
    except subprocess.TimeoutExpired:
        result["errors"].append(f"timeout after {scenario.timeout} seconds")
    except Exception as error:  # noqa: BLE001 - preserve the scenario failure in the report
        result["errors"].append(str(error))

    result["duration_seconds"] = round(time.time() - started, 2)
    destination = artifact_root / scenario.name
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        destination = artifact_root / f"{scenario.name}-{int(time.time())}"
    if (work_root / scenario.name).exists():
        shutil.copytree(work_root / scenario.name, destination)
    result["artifact_directory"] = str(destination)
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--no-build", dest="build", action="store_false", help="Use the existing simulator binary")
    parser.add_argument("--window", action="store_true", help="Show the SDL window during the scenarios")
    parser.add_argument("--scenario", help="Run only one named scenario")
    parser.add_argument("--font-source", help="Path to the CJK cpfont fixture")
    parser.add_argument("--artifact-root", type=Path, default=ROOT / "qa-artifacts" / "simulator-regression")
    parser.add_argument("--from-windows", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--internal-stage", action="store_true", help=argparse.SUPPRESS)
    parser.set_defaults(build=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if os.name == "nt" and not args.from_windows and not args.internal_stage:
        return run_from_windows(args)
    if os.name != "nt" and not args.internal_stage and ROOT.as_posix().startswith("/mnt/"):
        return stage_wsl_source(args)

    args.artifact_root.mkdir(parents=True, exist_ok=True)
    scenarios = selected_scenarios(args.scenario)
    if args.build:
        build_status = build_simulator(args.artifact_root)
        if build_status != 0:
            return build_status

    program = ROOT / ".pio" / "build" / "simulator_x3" / "program"
    if not program.is_file():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        return 2

    run_id = time.strftime("%Y%m%d-%H%M%S")
    work_root = ROOT / ".qa-simulator-runs" / run_id
    artifact_run_root = args.artifact_root / run_id
    work_root.mkdir(parents=True, exist_ok=True)
    artifact_run_root.mkdir(parents=True, exist_ok=True)
    summary = {
        "reference_release": REFERENCE_RELEASE,
        "tested_version": read_version(),
        "root": str(ROOT),
        "started_at": run_id,
        "scenarios": [],
    }

    for scenario in scenarios:
        print(f"Running {scenario.name}...", flush=True)
        outcome = run_scenario(work_root, artifact_run_root, scenario, args.font_source, args.window)
        summary["scenarios"].append(outcome)
        status = outcome["status"].upper()
        print(f"{status}: {scenario.name} ({outcome['duration_seconds']} s)", flush=True)
        if outcome["errors"]:
            for error in outcome["errors"]:
                print(f"  {error}", file=sys.stderr)

    summary["passed"] = sum(item["status"] == "passed" for item in summary["scenarios"])
    summary["failed"] = len(summary["scenarios"]) - summary["passed"]
    summary_path = artifact_run_root / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"Regression report: {summary_path}", flush=True)
    print(f"Result: {summary['passed']} passed, {summary['failed']} failed", flush=True)
    return 0 if summary["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
