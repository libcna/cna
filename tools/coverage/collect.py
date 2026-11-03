#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Capture one source/configuration-matched Ninja/GCC GoogleTest coverage run."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


class CoverageError(RuntimeError):
    """Evidence is missing, failed, stale or incompatible."""


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def execute(command, cwd=None):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    if result.returncode:
        raise CoverageError(f'{shlex.join(command)}: exit {result.returncode}\n{result.stderr}')
    return result.stdout


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def cache_values(build):
    values = {}
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line and not line.startswith(('#', '//')) and '=' in line:
            key, value = line.split('=', 1)
            values[key.split(':')[0]] = value
    return values


def configuration(build):
    cache = cache_values(build)
    keys = {k: v for k, v in cache.items() if k.startswith(('CNA_', 'CMAKE_'))}
    return {'cache': keys,
            'files': {str(build / f): digest(build / f) for f in
                      ('CMakeCache.txt', 'compile_commands.json', 'build.ninja')},
            'compilers': {lang: execute([cache[f'CMAKE_{lang}_COMPILER'], '--version'])
                          for lang in ('C', 'CXX')}}


def revision(source):
    return {'head': execute(['git', 'rev-parse', 'HEAD'], source).strip(),
            'dirty': bool(execute(['git', 'status', '--porcelain'], source).strip())}


def prepare(build, targets, output):
    if not targets or len(set(targets)) != len(targets):
        raise CoverageError('Select nonempty, unique Ninja test targets')
    source = Path(cache_values(build)['CMAKE_HOME_DIRECTORY']).resolve()
    inputs = {str((build / p).resolve()) for p in
              execute(['ninja', '-t', 'inputs', *targets], build).splitlines()}
    units = []
    sources = set()
    include_dirs = set()
    for unit in json.loads((build / 'compile_commands.json').read_text()):
        obj = (Path(unit['directory']) / unit['output']).resolve()
        if str(obj) not in inputs:
            continue
        args = shlex.split(unit['command'])
        if '--coverage' not in args:
            raise CoverageError(f'Uninstrumented compilation: {obj}')
        units.append({'object': str(obj), 'source': str(Path(unit['file']).resolve()),
                      'command': unit['command']})
        sources.add(Path(unit['file']).resolve())
        include_dirs.add(Path(unit['file']).resolve().parent)
        for index, arg in enumerate(args):
            if arg == '-isystem' or arg == '-I':
                include_dirs.add((Path(unit['directory']) / args[index + 1]).resolve())
            elif arg.startswith('-I'):
                include_dirs.add((Path(unit['directory']) / arg[2:]).resolve())
    if not units:
        raise CoverageError('No compilation units for the selected targets')
    # Include headers/templates even before Ninja has populated its dependency log.
    # Avoid repeatedly traversing nested include roots from many compilation units.
    roots = [p for p in include_dirs if not any(q != p and p.is_relative_to(q)
                                               for q in include_dirs)]
    for directory in roots:
        for suffix in ('*.h', '*.hpp', '*.inl', '*.inc', '*.tpp', '*.ipp', '*.tcc'):
            sources.update(p.resolve() for p in directory.rglob(suffix))
    sources.update(Path(p) for p in inputs if Path(p).is_file() and
                   Path(p).suffix in ('.h', '.hpp', '.cpp', '.c', '.cc', '.inc'))
    data = {'schema': 1, 'build': str(build), 'source': str(source), 'targets': targets,
            'configuration': configuration(build), 'revision': revision(source),
            'sources': {str(p): digest(p) for p in sorted(sources)}, 'units': units,
            'prepared_at': time.time()}
    save(output, data)
    print(f'Prepared {len(units)} units and {len(sources)} source/header hashes')


def verify_prepared(prepared):
    if configuration(Path(prepared['build'])) != prepared['configuration']:
        raise CoverageError('Build configuration changed; prepare and build again')
    for path, expected in prepared['sources'].items():
        if not Path(path).is_file() or digest(path) != expected:
            raise CoverageError(f'Source/header changed since preparation: {path}')


def test_results(path):
    root = ET.parse(path).getroot()
    cases = []
    for suite in root.iter('testsuite'):
        for case in suite.findall('testcase'):
            status = 'passed'
            if case.find('failure') is not None or case.find('error') is not None:
                status = 'failed'
            elif case.find('skipped') is not None or case.get('result') == 'skipped':
                status = 'skipped'
            elif case.get('status') == 'notrun' or case.get('result') == 'suppressed':
                status = 'disabled'
            cases.append({'name': suite.get('name') + '.' + case.get('name'),
                          'status': status, 'seconds': case.get('time'),
                          'message': '\n'.join(child.get('message', child.text or '')
                                               for child in case)})
    return cases


def validate_cases(cases, path):
    if not cases or not any(c['status'] == 'passed' for c in cases):
        raise CoverageError(f'Empty or entirely skipped/disabled run: {path}')
    if any(c['status'] == 'failed' for c in cases):
        raise CoverageError(f'Failed tests: {path}')


def classify(path, source, build):
    if path.is_relative_to(build):
        return None, 'generated/build-tree source'
    if not path.is_relative_to(source):
        return None, 'external dependency or system header'
    parts = path.relative_to(source).parts
    for name, reason in (('third_party', 'third-party dependency'),
                         ('vendor', 'vendored dependency'),
                         ('generated', 'generated source'),
                         ('tests', 'test code'), ('tools', 'tool code')):
        if name in parts:
            return None, reason
    if len(parts) >= 4 and parts[0] == 'modules':
        index = 3 if parts[1] == 'renderers' else 2
        if len(parts) > index + 1 and parts[index] in ('src', 'include'):
            return ('/'.join(parts[1:index]), parts[index]), None
    return None, 'outside module production src/include'


def metric(values):
    total = len(values)
    covered = sum(v > 0 for v in values)
    return {'covered': covered, 'total': total,
            'percent': round(100 * covered / total, 2) if total else None}


def format_metric(value):
    percent = f"{value['percent']}%" if value['percent'] is not None else 'n/a'
    return f"{value['covered']}/{value['total']} ({percent})"


def gcov_document(result, note):
    if result.returncode:
        raise CoverageError(f'gcov rejected {note}: {result.stderr}')
    document = json.loads(result.stdout)
    allowed = {f'{note.with_suffix(".gcda")}:cannot open data file, assuming not executed'}
    if not document['files']:
        allowed.add(f'{note}:no functions found')
    if any(line not in allowed for line in result.stderr.strip().splitlines()):
        raise CoverageError(f'gcov rejected {note}: {result.stderr}')
    return document


class Aggregator:
    """Union line/function hits; retain branch instances per compilation unit."""

    def __init__(self, source, build, gcc_version):
        self.source, self.build, self.gcc_version = source, build, gcc_version
        self.files = {}
        self.excluded = {}
        self.units = set()

    def add(self, document, unit):
        if unit in self.units:
            raise CoverageError('Duplicate compilation unit in gcov inputs')
        if document.get('format_version') != '2':
            raise CoverageError('Only GCC gcov JSON format 2 is supported')
        if document.get('gcc_version') != self.gcc_version:
            raise CoverageError('Mixed compiler versions in gcov inputs')
        reported = Path(document['data_file'])
        if reported.resolve() != Path(unit).with_suffix('.gcno').resolve():
            raise CoverageError('gcov input does not match the selected compilation unit')
        self.units.add(unit)
        for record in document['files']:
            path = (Path(document['current_working_directory']) / record['file']).resolve()
            group, reason = classify(path, self.source, self.build)
            if reason:
                self.excluded[str(path)] = reason
                continue
            name = str(path.relative_to(self.source))
            file = self.files.setdefault(name, {'module': group[0], 'area': group[1],
                                              'lines': {}, 'functions': {}, 'branches': {}})
            for function in record['functions']:
                key = (function['name'], function['start_line'], function['start_column'])
                file['functions'][key] = file['functions'].get(key, 0) + function['execution_count']
            for line in record['lines']:
                number = line['line_number']
                file['lines'][number] = file['lines'].get(number, 0) + line['count']
                # GCC block IDs are local to a compilation unit. Do not coalesce arcs
                # from different template instantiations or differently compiled TUs.
                for index, branch in enumerate(line['branches']):
                    key = (unit, line.get('function_name', ''), number, index,
                           branch['source_block_id'], branch['destination_block_id'],
                           branch['throw'], branch['fallthrough'])
                    if key in file['branches']:
                        raise CoverageError('Duplicate gcov input/branch instance')
                    file['branches'][key] = branch['count']

    def result(self):
        modules = {}
        files = {}
        for path, data in sorted(self.files.items()):
            stats = {key: metric(data[key].values()) for key in ('functions', 'lines', 'branches')}
            uncovered = {}
            for key, count in data['branches'].items():
                if not count:
                    cluster = (key[2], key[1])
                    pair = uncovered.setdefault(cluster, [0, 0])
                    pair[0] += 1
                    pair[1] += bool(key[6])
            files[path] = {'module': data['module'], 'area': data['area'], **stats,
                           'uncovered_lines': sorted(k for k, v in data['lines'].items() if not v),
                           'uncovered_functions': [
                               {'name': k[0], 'line': k[1], 'column': k[2]}
                               for k, v in sorted(data['functions'].items()) if not v],
                           'uncovered_branches': [
                               {'line': line, 'function': function, 'uncovered': counts[0],
                                'exception_arcs': counts[1], 'line_hits': data['lines'][line]}
                               for (line, function), counts in sorted(
                                   uncovered.items(), key=lambda item: (-item[1][0], item[0]))]}
            area = modules.setdefault(data['module'], {}).setdefault(data['area'],
                {key: {'covered': 0, 'total': 0} for key in stats})
            for key in stats:
                for count in ('covered', 'total'):
                    area[key][count] += stats[key][count]
        for module in modules.values():
            for area in module.values():
                for stats in area.values():
                    stats['percent'] = (round(100 * stats['covered'] / stats['total'], 2)
                                        if stats['total'] else None)
        if not files or not any(f['lines']['covered'] for f in files.values()):
            raise CoverageError('No executed CNA production lines')
        return {'modules': modules, 'files': files,
                'excluded_paths': [{'path': p, 'reason': r} for p, r in sorted(self.excluded.items())]}


def markdown(report):
    prepared = report['prepared']
    config = prepared['configuration']['cache']
    rows = ['# MTH-TEST-002 coverage baseline', '',
            f"Configuration: {config['CNA_GRAPHICS_RENDERER']} renderer / "
            f"{config['CNA_PLATFORM']} platform, {config['CMAKE_BUILD_TYPE']}.",
            f"Compiler: {prepared['configuration']['compilers']['CXX'].splitlines()[0]}.",
            f"C/C++ flags: `{config['CMAKE_C_FLAGS']}` / `{config['CMAKE_CXX_FLAGS']}`; "
            f"Debug flags: `{config['CMAKE_CXX_FLAGS_DEBUG']}`.",
            f"Executable/shared link flags: `{config['CMAKE_EXE_LINKER_FLAGS']}` / "
            f"`{config['CMAKE_SHARED_LINKER_FLAGS']}`; shared library: {config['CNA_SHARED_LIBRARY']}.",
            f"Prepared revision: `{prepared['revision']['head']}`; dirty: {prepared['revision']['dirty']}.",
            f"Run revision: `{report['revision']['head']}`; dirty: {report['revision']['dirty']}.",
            f"End revision: `{report['revision_after']['head']}`; dirty: {report['revision_after']['dirty']}.",
            '', 'Compiler, flags, source hashes, artifact hashes, exact commands, individual test',
            'outcomes and excluded paths/reasons are retained in coverage.json and run.json.', '',
            'Coverage measures execution, not correctness. Source lines and mangled functions',
            'are unioned across translation units. Branches count GCC arc instances per',
            'translation unit (including exception arcs); they are not unique source decisions.',
            'Module include/ headers/templates are separate from src/; private headers',
            'under src/ remain in its totals.',
            'Unbuilt source paths are excluded explicitly; these are selected-build denominators.', '',
            '| Module | Area | Functions | Lines | Branch instances |',
            '|---|---|---:|---:|---:|']
    for module, areas in sorted(report['modules'].items()):
        for area, stats in sorted(areas.items()):
            values = [format_metric(stats[k]) for k in ('functions', 'lines', 'branches')]
            rows.append(f'| {module} | {area} | ' + ' | '.join(values) + ' |')
    rows.extend(['', '| Test binary | Passed | Failed | Skipped | Disabled |',
                 '|---|---:|---:|---:|---:|'])
    for run in report['runs']:
        counts = [sum(c['status'] == s for c in run['cases'])
                  for s in ('passed', 'failed', 'skipped', 'disabled')]
        rows.append(f"| {run['target']} | " + ' | '.join(map(str, counts)) + ' |')
    return '\n'.join(rows) + '\n'


def collect(prepared_path, output, gcov):
    output.mkdir(parents=True, exist_ok=True)
    reports = [output / 'coverage.json', output / 'COVERAGE_BASELINE.md']
    for path in reports:
        path.unlink(missing_ok=True)
    (output / 'run.json').unlink(missing_ok=True)
    prepared = json.loads(prepared_path.read_text())
    build, source = Path(prepared['build']), Path(prepared['source'])
    verify_prepared(prepared)
    cache = prepared['configuration']['cache']
    if cache['CNA_PLATFORM'] != 'HEADLESS' or cache['CNA_GRAPHICS_RENDERER'] != 'HEADLESS':
        raise CoverageError('This runner requires HEADLESS platform and renderer')
    # Let Ninja prove that every requested artifact matches this configuration;
    # merely finding old .gcno files would permit mixing configurations.
    build_command = ['cmake', '--build', str(build), '--parallel', '4',
                     '--target', *prepared['targets']]
    build_start = time.monotonic()
    with (output / 'coverage-build.log').open('w') as log:
        built = subprocess.run(build_command, stdout=log, stderr=subprocess.STDOUT)
    build_seconds = time.monotonic() - build_start
    if built.returncode:
        raise CoverageError(f'Coverage build failed: exit {built.returncode}')
    verify_prepared(prepared)
    gcov_version = execute([gcov, '--version'])
    gcc_version = execute([cache['CMAKE_CXX_COMPILER'], '-dumpfullversion']).strip()
    units = prepared['units']
    artifacts = {}
    for unit in units:
        obj = Path(unit['object'])
        for path in (obj, obj.with_suffix('.gcno')):
            if not path.is_file():
                raise CoverageError(f'Missing instrumented build artifact: {path}')
            artifacts[str(path)] = digest(path)
    for target in prepared['targets']:
        artifacts[str(build / target)] = digest(build / target)
    for name in execute(['ninja', '-t', 'inputs', *prepared['targets']], build).splitlines():
        path = build / name
        if path.suffix in ('.a', '.so', '.dylib', '.dll') or '.so.' in path.name:
            artifacts[str(path)] = digest(path)
    # Clear counters only for this selected profile, including previously linked units.
    for unit in units:
        Path(unit['object']).with_suffix('.gcda').unlink(missing_ok=True)
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(('GTEST_', 'GCOV_')) and k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    env.update(SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy')
    evidence = {'prepared': prepared, 'revision': revision(source), 'artifacts': artifacts,
                'gcov_version': gcov_version, 'runs': [], 'status': 'running',
                'build_command': build_command, 'build_exit_code': built.returncode,
                'environment': {k: env.get(k) for k in
                                ('SDL_VIDEODRIVER', 'SDL_AUDIODRIVER', 'TMPDIR',
                                 'CCACHE_DIR', 'CCACHE_BASEDIR')}}
    save(output / 'run.json', evidence)
    try:
        failures = []
        for target in prepared['targets']:
            xml = output / f'{target}.xml'
            xml.unlink(missing_ok=True)
            command = ['env', '-u', 'DISPLAY', '-u', 'WAYLAND_DISPLAY',
                       'SDL_VIDEODRIVER=offscreen', 'SDL_AUDIODRIVER=dummy',
                       'timeout', '1800', str(build / target), f'--gtest_output=xml:{xml}']
            start = time.monotonic()
            with (output / f'{target}.log').open('w') as log:
                result = subprocess.run(command, cwd=source, env=env, stdout=log,
                                        stderr=subprocess.STDOUT)
            run = {'target': target, 'command': command, 'cwd': str(source),
                   'exit_code': result.returncode, 'seconds': time.monotonic() - start,
                   'log_sha256': digest(output / f'{target}.log')}
            evidence['runs'].append(run)
            save(output / 'run.json', evidence)
            try:
                run['cases'] = test_results(xml)
                run['xml_sha256'] = digest(xml)
                validate_cases(run['cases'], xml)
                if result.returncode:
                    raise CoverageError(f'{target} failed: exit {result.returncode}; see its log')
            except (CoverageError, OSError, ET.ParseError) as error:
                run['error'] = str(error)
                failures.append(f'{target}: {error}')
            print(f'{target}: {len(run.get("cases", []))} cases, exit {result.returncode}', flush=True)
            save(output / 'run.json', evidence)
        if failures:
            raise CoverageError('\n'.join(failures))
        verify_prepared(prepared)
        for path, expected in artifacts.items():
            if digest(path) != expected:
                raise CoverageError(f'Build artifact changed during execution: {path}')
        aggregator = Aggregator(source, build, gcc_version)
        profiles = {}
        for unit in units:
            data = Path(unit['object']).with_suffix('.gcda')
            profiles[str(data)] = digest(data) if data.exists() else None
        for index, unit in enumerate(units):
            note = Path(unit['object']).with_suffix('.gcno')
            data = note.with_suffix('.gcda')
            result = subprocess.run([gcov, '--json-format', '--stdout', '--branch-counts',
                                     '--branch-probabilities', str(note)],
                                    cwd=build, text=True, capture_output=True)
            document = gcov_document(result, note)
            if not document['files']:
                aggregator.excluded[unit['source']] = 'no instrumentable functions in this configuration'
            aggregator.add(document, unit['object'])
            if index % 100 == 0:
                print(f'gcov: {index + 1}/{len(units)} units', flush=True)
        for path, expected in profiles.items():
            actual = digest(path) if Path(path).exists() else None
            if actual != expected:
                raise CoverageError(f'Coverage counters changed during aggregation: {path}')
        verify_prepared(prepared)
        for path, expected in artifacts.items():
            if digest(path) != expected:
                raise CoverageError(f'Build artifact changed during aggregation: {path}')
        result = aggregator.result()
        # Files absent from gcov are not implicitly covered or in its denominator.
        measured = set(result['files'])
        for path in sorted((source / 'modules').rglob('*')):
            if path.suffix not in ('.cpp', '.c', '.cc', '.h', '.hpp', '.inl', '.inc'):
                continue
            group, reason = classify(path, source, build)
            if group and str(path.relative_to(source)) not in measured:
                result['excluded_paths'].append({'path': str(path),
                    'reason': 'not emitted in selected compilation units'})
        evidence.update(status='passed', profiles=profiles, revision_after=revision(source),
                        build_seconds=build_seconds)
        save(output / 'run.json', evidence)
        report = {'schema': 1, **evidence, **result}
        save(reports[0], report)
        reports[1].write_text(markdown(report))
    except Exception as error:
        for path in reports:
            path.unlink(missing_ok=True)
        evidence.update(status='failed', error=str(error))
        save(output / 'run.json', evidence)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    prep = sub.add_parser('prepare', help='Snapshot configured sources before building')
    prep.add_argument('--build-dir', type=Path, required=True)
    prep.add_argument('--output', type=Path, required=True)
    prep.add_argument('targets', nargs='+')
    run = sub.add_parser('run', help='Clear counters, run tests, validate and aggregate')
    run.add_argument('--prepared', type=Path, required=True)
    run.add_argument('--output-dir', type=Path, required=True)
    run.add_argument('--gcov', default='gcov')
    args = parser.parse_args()
    try:
        if args.action == 'prepare':
            prepare(args.build_dir.resolve(), args.targets, args.output)
        else:
            collect(args.prepared.resolve(), args.output_dir.resolve(), args.gcov)
    except (CoverageError, OSError, ValueError, ET.ParseError) as error:
        print(f'coverage: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
