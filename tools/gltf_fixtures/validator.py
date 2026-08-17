# SPDX-License-Identifier: MS-PL
"""Pinned Khronos glTF Validator integration for the generated corpus (GLTF-015).

The Python generator remains standard-library-only and offline by default. A caller that supplies
``--validator`` opts into this gate; CI downloads the immutable native release named by
``validator-pin.json`` and verifies its archive digest before invoking it.
"""

from __future__ import annotations

import hashlib
import io
import json
import platform
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from typing import Any, Sequence

from .manifest import Fixture


VALIDATOR_PIN_PATH = Path(__file__).with_name("validator-pin.json")


class ValidatorError(ValueError):
    """The validator pin, executable, report, or corpus result violates its contract."""


def _read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def _is_sha256(value: Any) -> bool:
    return (isinstance(value, str) and len(value) == 64
            and all(character in "0123456789abcdef" for character in value))


def load_validator_pin() -> dict[str, Any]:
    """Loads and structurally validates the committed official-release pin."""
    document = _read_json(VALIDATOR_PIN_PATH)
    if not isinstance(document, dict) or document.get("schemaVersion") != 1:
        raise ValidatorError("validator-pin.json must be a schemaVersion 1 object")
    expected = {
        "task": "GLTF-015",
        "name": "Khronos glTF Validator",
        "repository": "https://github.com/KhronosGroup/glTF-Validator",
        "releaseVersion": "2.0.0-dev.3.10",
        "runtimeDependency": False,
        "ciDependency": True,
    }
    for key, value in expected.items():
        if document.get(key) != value:
            raise ValidatorError(f"validator-pin.json {key} does not match the GLTF-015 pin")
    license_record = document.get("license")
    if (not isinstance(license_record, dict)
            or license_record.get("spdx") != "Apache-2.0"
            or not isinstance(license_record.get("summary"), str)
            or not license_record["summary"]):
        raise ValidatorError("validator-pin.json has no valid Apache-2.0 licence record")
    artifact = document.get("ciArtifact")
    if not isinstance(artifact, dict):
        raise ValidatorError("validator-pin.json ciArtifact must be an object")
    archive = f"gltf_validator-{expected['releaseVersion']}-linux64.tar.xz"
    expected_url = (f"{expected['repository']}/releases/download/"
                    f"{expected['releaseVersion']}/{archive}")
    if (artifact.get("platform") != "linux-x86_64"
            or artifact.get("archive") != archive
            or artifact.get("url") != expected_url
            or artifact.get("executable") != "gltf_validator"
            or not _is_sha256(artifact.get("sha256"))):
        raise ValidatorError("validator-pin.json ciArtifact is incomplete or inconsistent")
    return document


def install_pinned_validator(destination: Path) -> Path:
    """Downloads, verifies, and extracts the pinned Linux CI executable into ``destination``."""
    pin = load_validator_pin()
    artifact = pin["ciArtifact"]
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
        raise ValidatorError(
            "the pinned CI artifact is linux-x86_64; install the same Validator release "
            "manually on this platform and pass it through --validator")
    try:
        with urllib.request.urlopen(artifact["url"], timeout=60) as response:
            archive_bytes = response.read()
    except OSError as error:
        raise ValidatorError(f"cannot download {artifact['url']}: {error}") from error
    actual_digest = hashlib.sha256(archive_bytes).hexdigest()
    if actual_digest != artifact["sha256"]:
        raise ValidatorError(
            f"downloaded Validator archive SHA-256 {actual_digest} does not match pinned "
            f"{artifact['sha256']}")
    try:
        with tarfile.open(fileobj=io.BytesIO(archive_bytes), mode="r:xz") as archive:
            member = archive.getmember(artifact["executable"])
            if not member.isfile() or member.issym() or member.islnk():
                raise ValidatorError("pinned Validator executable is not a regular archive file")
            source = archive.extractfile(member)
            if source is None:
                raise ValidatorError("pinned Validator executable cannot be read from its archive")
            executable_bytes = source.read()
    except (KeyError, tarfile.TarError) as error:
        raise ValidatorError(f"invalid pinned Validator archive: {error}") from error
    destination.mkdir(parents=True, exist_ok=True)
    executable = destination / artifact["executable"]
    executable.write_bytes(executable_bytes)
    executable.chmod(0o755)
    return executable


def _report(executable: Path, asset: Path) -> tuple[subprocess.CompletedProcess[str], dict[str, Any]]:
    command = [str(executable), "--stdout", "--no-write-timestamp", "--no-absolute-path",
               str(asset)]
    try:
        result = subprocess.run(command, check=False, capture_output=True, text=True)
    except OSError as error:
        raise ValidatorError(f"cannot execute {executable}: {error}") from error
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        detail = result.stderr.strip() or result.stdout.strip() or "no output"
        raise ValidatorError(f"{asset.name}: Validator returned no JSON report: {detail}") from error
    if not isinstance(report, dict):
        raise ValidatorError(f"{asset.name}: Validator report is not a JSON object")
    return result, report


def validate_emission(fixtures: Sequence[Fixture], files: dict[str, bytes],
                      executable: Path) -> dict[str, int | str]:
    """Validates both containers of every emitted fixture against exact error-code oracles."""
    pin = load_validator_pin()
    expected_version = str(pin["releaseVersion"])
    try:
        version = subprocess.run([str(executable), "--version"], check=False,
                                 capture_output=True, text=True)
    except OSError as error:
        raise ValidatorError(f"cannot execute {executable}: {error}") from error
    version_text = version.stdout + version.stderr
    # The 3.10 native CLI prints its version banner before usage for an unknown `--version` flag
    # and exits with EX_USAGE; the banner is nevertheless the only version query it exposes.
    if f"version {expected_version}" not in version_text:
        raise ValidatorError(
            f"{executable}: expected glTF Validator {expected_version}; got "
            f"{version_text.strip() or 'no version output'}")

    problems: list[str] = []
    valid_containers = 0
    expected_invalid_containers = 0
    warning_count = 0
    with tempfile.TemporaryDirectory(prefix="cna-gltf-validator-") as temporary:
        root = Path(temporary)
        for name, data in files.items():
            (root / name).write_bytes(data)

        for fixture in fixtures:
            expected_codes = set(fixture.validator_expected_errors)
            for suffix in (".gltf", ".glb"):
                asset = root / f"{fixture.id}{suffix}"
                result, report = _report(executable, asset)
                if report.get("validatorVersion") != expected_version:
                    problems.append(
                        f"{asset.name}: report version {report.get('validatorVersion')!r} is not "
                        f"the pinned {expected_version}")
                    continue
                issues = report.get("issues")
                if not isinstance(issues, dict) or not isinstance(issues.get("messages"), list):
                    problems.append(f"{asset.name}: report has no issues.messages array")
                    continue
                error_codes = {
                    str(message.get("code"))
                    for message in issues["messages"]
                    if isinstance(message, dict) and message.get("severity") == 0
                }
                reported_errors = issues.get("numErrors")
                if not isinstance(reported_errors, int) or reported_errors < len(error_codes):
                    problems.append(f"{asset.name}: invalid issues.numErrors {reported_errors!r}")
                    continue
                warnings = issues.get("numWarnings", 0)
                if isinstance(warnings, int):
                    warning_count += warnings
                if error_codes != expected_codes:
                    problems.append(
                        f"{asset.name}: Validator error codes {sorted(error_codes)}; expected "
                        f"{sorted(expected_codes)}")
                if bool(reported_errors) != (result.returncode != 0):
                    problems.append(
                        f"{asset.name}: exit {result.returncode} disagrees with "
                        f"numErrors={reported_errors}")
                if expected_codes:
                    expected_invalid_containers += 1
                else:
                    valid_containers += 1

    if problems:
        raise ValidatorError("\n".join(problems))
    return {
        "validatorVersion": expected_version,
        "fixtureCount": len(fixtures),
        "containerCount": len(fixtures) * 2,
        "validContainers": valid_containers,
        "expectedInvalidContainers": expected_invalid_containers,
        "warningCount": warning_count,
    }
