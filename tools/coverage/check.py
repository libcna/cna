#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Check a fresh, successful coverage collection against reviewed module floors."""

import argparse
import json
import math
from pathlib import Path
import subprocess
import sys

from collect import CoverageError, classify, digest, gcov_document, validate_cases, verify_prepared


def counter_quality(report, policy):
    """Reject impossible gcov counts in the gated module/area source units."""
    source = Path(report['prepared']['source'])
    build = Path(report['prepared']['build'])
    for unit in report['prepared']['units']:
        group, _ = classify(Path(unit['source']), source, build)
        if not group or group[1] not in policy['modules'].get(group[0], {}):
            continue
        note = Path(unit['object']).with_suffix('.gcno')
        result = subprocess.run(['gcov', '--json-format', '--stdout', '--branch-counts',
                                 '--branch-probabilities', str(note)], cwd=build,
                                text=True, capture_output=True)
        document = gcov_document(result, note)
        for file in document['files']:
            path = (Path(document['current_working_directory']) / file['file']).resolve()
            group, _ = classify(path, source, build)
            if not group or group[1] not in policy['modules'].get(group[0], {}):
                continue
            counts = [f['execution_count'] for f in file['functions']]
            counts += [line['count'] for line in file['lines']]
            counts += [branch['count'] for line in file['lines'] for branch in line['branches']]
            if any(count < 0 for count in counts):
                raise CoverageError(f'Negative gcov counter in gated module: {path}')


def check(report, policy):
    """Validate evidence and return exact (unrounded) per-metric comparisons."""
    try:
        if report['schema'] != 1 or policy['schema'] != 1:
            raise CoverageError('Unsupported report or threshold schema')
        if report['status'] != 'passed' or report['build_exit_code'] != 0:
            raise CoverageError('Coverage collection/build did not pass')
        prepared = report['prepared']
        targets = prepared['targets']
        if (not targets or len(targets) != len(set(targets))
                or sorted(targets) != sorted(policy['targets'])):
            raise CoverageError('Test selection differs from the reviewed policy')
        for key, value in policy['configuration'].items():
            if prepared['configuration']['cache'].get(key) != value:
                raise CoverageError(f'Incompatible coverage configuration: {key}')
        if not report['gcov_version'].splitlines()[0].endswith(policy['gcc_version']):
            raise CoverageError('Incompatible gcov version')
        runs = report['runs']
        if sorted(r['target'] for r in runs) != sorted(targets):
            raise CoverageError('Missing or duplicate test runs')
        for run in runs:
            validate_cases(run['cases'], run['target'])
            if run['exit_code'] != 0:
                raise CoverageError(f"Failed test process: {run['target']}")
        if not report['artifacts'] or not report['profiles']:
            raise CoverageError('Missing artifact/counter provenance')
        verify_prepared(prepared)
        for path, expected in {**report['artifacts'], **report['profiles']}.items():
            actual = digest(path) if Path(path).is_file() else None
            if actual != expected:
                raise CoverageError(f'Artifact/counter changed since collection: {path}')
        rules = policy['modules']
        if not rules:
            raise CoverageError('Empty threshold policy')
        counter_quality(report, policy)
        results = []
        for module, areas in rules.items():
            for area, metrics in areas.items():
                if not metrics:
                    raise CoverageError(f'Empty thresholds: {module}/{area}')
                for metric, rule in metrics.items():
                    if metric not in ('functions', 'lines', 'branches'):
                        raise CoverageError(f'Unknown metric: {metric}')
                    floor, minimum = rule['minimum_percent'], rule['minimum_total']
                    if (type(floor) not in (int, float) or not math.isfinite(floor)
                            or not 0 <= floor <= 100 or type(minimum) is not int or minimum <= 0):
                        raise CoverageError('Invalid percentage/denominator threshold')
                    value = report['modules'][module][area][metric]
                    covered, total = value['covered'], value['total']
                    if (type(total) is not int or type(covered) is not int
                            or total < minimum or not 0 <= covered <= total):
                        raise CoverageError(f'Missing/invalid/shrunken denominator: {module}/{area}/{metric}')
                    percent = 100 * covered / total
                    results.append({'module': module, 'area': area, 'metric': metric,
                                    'covered': covered, 'total': total, 'percent': percent,
                                    'minimum_percent': floor, 'passed': percent >= floor})
        return results
    except (KeyError, TypeError, AttributeError, IndexError) as error:
        raise CoverageError(f'Malformed or incomplete coverage evidence/policy: {error}') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--thresholds', type=Path,
                        default=Path(__file__).with_name('thresholds.json'))
    args = parser.parse_args()
    try:
        results = check(json.loads(args.report.read_text()), json.loads(args.thresholds.read_text()))
        for result in results:
            print('{module}/{area}/{metric}: {covered}/{total} = {percent:.3f}% '
                  '(floor {minimum_percent}%) {outcome}'.format(
                      **result, outcome='PASS' if result['passed'] else 'FAIL'))
        return 0 if all(r['passed'] for r in results) else 1
    except (CoverageError, OSError, ValueError) as error:
        print(f'coverage check: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
