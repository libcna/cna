#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Inventory campaign-added test definitions and their reviewed CI execution lanes.

Definitions/registrations are source evidence, never proof of executed assertions.
Parameterized definitions are retained once; expanded runtime cases live in coverage.json.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

BASE = '3f8145997^'
GTEST = re.compile(r'^\s*(TEST(?:_F|_P)?|TYPED_TEST(?:_P)?)\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)', re.M)
PYTEST = re.compile(r'^\s*def (test_\w+)\(', re.M)


def git(source, *args):
    return subprocess.check_output(['git', '-C', str(source), *args], text=True)


def definitions(text):
    return ([{'kind': m[1], 'name': m[2] + '.' + m[3],
              'line': text.count('\n', 0, m.start()) + 1} for m in GTEST.finditer(text)] +
            [{'kind': 'unittest', 'name': m[1], 'line': text.count('\n', 0, m.start()) + 1}
             for m in PYTEST.finditer(text)])


def c_api_registrations(cmake):
    """Expand literal-mode CMake loops used by the native strict-C suites."""
    pattern = r'add_test\(\s*NAME\s+([^\s()]+)\s+COMMAND\s+([^)]*)\)'
    result = [(m[1].strip('"'), m[2]) for m in re.finditer(pattern, cmake)
              if '${' not in m[1]]
    for loop in re.finditer(r'foreach\(\s*(\w+)\s+([^)]*)\)(.*?)endforeach\(\)', cmake, re.S):
        values = re.sub(r'#[^\n]*', '', loop[2]).split()
        if values[:2] == ['IN', 'ITEMS']:
            values = values[2:]
        if not values or values[0] == 'IN' or any('${' in value for value in values):
            continue
        token = '${' + loop[1] + '}'
        for registration in re.finditer(pattern, loop[3]):
            if token not in registration[1]:
                continue
            result += [(registration[1].strip('"').replace(token, value),
                        registration[2].replace(token, value)) for value in values]
    return result


def lane(path):
    if path.startswith('modules/c-api/'):
        return ('general-tests-ci.yml: CNA_BUILD_C_API=ON; build all; unfiltered ctest',
                'Previously disabled in general CI; header/route metadata workflows did not execute these suites. '
                'Value/CPU cases are deterministic; --runtime and window/device sublegs require the job display/device and may refuse/skip.')
    if path.startswith('modules/renderers/webgpu/'):
        return (None, 'Owner private WEBGPU run; no workflow configures WEBGPU. Requires a real GPU; no coverage credit from HEADLESS.')
    if path.endswith('/spritebatch_subpixel_contract_test.cpp'):
        return ('general-tests-ci.yml: EasyGL_SpriteBatchSubpixelContract via unfiltered ctest',
                'Shared registry now registers the byte-exact contract on each renderer family; '
                'other renderer legs are owner private runs, not this HEADLESS collection.')
    if path.startswith('modules/graphics/examples/conformance/'):
        return ('general-tests-ci.yml: OPENGLES3; build all; unfiltered ctest (EasyGL_Conformance_* registry)',
                'Other renderer legs remain owner private runs; unsupported capabilities may exit 77. No GPU run by TEST-012.')
    if path.startswith('modules/renderers/easygl/'):
        return ('general-tests-ci.yml: OPENGLES3, compiled effects default ON; unfiltered ctest',
                'Requires private display/GPU locally; software Mesa CI is a separate qualification domain.')
    if path.startswith('tools/fna-reference/test_value_edges'):
        return (None, 'Requires fresh independent FNA_EDGE_REFERENCE/CNA_EDGE_CANDIDATE dumps; without them class skips. '
                'Retained owner CONF-003 recipe, not a passing CI comparison.')
    if path.startswith('tools/fna-reference/test_run_reference'):
        return ('general-tests-ci.yml: python3 tools/fna-reference/test_run_reference.py -v',
                'New direct device-free recipe safety selection; does not execute FNA.')
    if path.startswith('tools/fna-reference/'):
        return (None, 'Independent FNA oracle generation is owner-run with pinned checkout/runtime. '
                'Fixtures/provenance, not a native test count; no automatic golden refresh.')
    if path.startswith('tools/coverage/'):
        return ('general-tests-ci.yml: python3 -m unittest discover -s tools/coverage -v',
                'Collector/checker control tests only; instrumented suite remains a local gate.')
    if path.startswith('tools/evidence/'):
        return ('general-tests-ci.yml: ExecutionEvidenceToolTests via unfiltered ctest',
                'Metadata/runtime evidence validation controls and fixtures, not CNA behavior qualification.')
    if path == 'tools/graphics/test_compiled_effect_fuzzer.py':
        return (None, 'CompiledEffectFuzzerCliTests is registered only when FNA3D is compiled; '
                'no workflow includes FNA3D. Owner private GPU replay lane SAFE-003; CPU support controls run in robustness CI.')
    if path.startswith('tools/graphics/compiled_effect_fuzzer'):
        return ('robustness-sanitizers-ci.yml: ^CompiledEffectFuzzerSupportTests$; general unfiltered CTest',
                'CPU harness controls only. Full GPU replay remains owner SAFE-003.')
    if path.startswith('scripts/test_compare_fna_reference'):
        return ('general-tests-ci.yml: FnaReferenceComparisonToolTests via unfiltered ctest',
                'Fail-closed comparator controls, not fresh FNA parity.')
    if path.startswith('tools/c-api/test_check_route_test_coverage'):
        return ('general-tests-ci.yml: CApiRouteTestCoverageToolTests via unfiltered ctest', 'Tool tests; route presence is metadata, not behavior.')
    if path.startswith('tools/xna-pipeline-oracle/'):
        return ('general-tests-ci.yml: XnaPipelineFinalAuditToolTests via unfiltered ctest', 'Tool fixture/controls, not runtime qualification.')
    if path.startswith('cmake/Tests/'):
        if 'InstalledRuntimeProbe' in path or path.endswith('/ModuleProbes.cmake'):
            if path.endswith('/CheckRelocationAudit.py') or path.endswith('/ModuleProbes.cmake'):
                return ('general-tests-ci.yml: CnaInstalledRelocationAudit via unfiltered ctest',
                        'Pending integration registration; Linux host-only audit controls. '
                        'Full CnaInstalledCppPackage additionally requires CNA_INSTALL_CXX=ON, absent from workflows.')
            return (None, 'Installed SDK fixture owned by platform lane: CnaInstalledCppPackage requires '
                    'CNA_INSTALL_CXX=ON, which no current workflow enables. Owner native/relocated SDK run; '
                    'heavy nested build qualification, not a fast CPU selection gap.')
        name = ('CnaSharedRuntimeIncremental' if 'SharedRuntimeIncremental' in path else
                'CnaSdlPrebuiltPublication' if 'SdlPrebuiltPublication' in path else 'CnaSdlPrebuiltState')
        return (f'general-tests-ci.yml: {name} via unfiltered ctest',
                'Nested miniature build controls; source fixtures are not additional native case counts.')
    if '/tests/' in path and path.endswith(('.cpp', '.hpp')):
        return ('general-tests-ci.yml: CnaTests discovery, build all, unfiltered ctest',
                'Default enabled module guards apply (net/services/video enabled when dependencies found). '
                'Safe002 and ContentResourcePressure/Failure also run in robustness-sanitizers-ci.yml; '
                'input label additionally runs in input-ci.yml. Native platform/hardware and skips remain separate.')
    return (None, 'Supporting fixture/registration/tool change; not counted as an added test definition.')


def inventory(source, base=BASE):
    head = git(source, 'rev-parse', 'HEAD').strip()
    base = git(source, 'rev-parse', base).strip()
    commits = git(source, 'log', '--format=%H%x09%s', f'{base}..{head}', '--grep=MTH-').splitlines()
    owners = {}
    for commit in commits:
        sha, subject = commit.split('\t', 1)
        for path in git(source, 'diff-tree', '--no-commit-id', '--name-only', '-r', sha).splitlines():
            owners.setdefault(path, []).append({'commit': sha, 'subject': subject})
    # Include this lane and pending integration additions without pretending they are committed.
    dirty = git(source, 'status', '--porcelain', '--untracked-files=all')
    for entry in dirty.splitlines():
        path = entry[3:]
        if (path.startswith('tools/coverage/') or '/tests/' in path or '/Tests/' in path
                or path.endswith('_test.cpp')):
            owners.setdefault(path, []).insert(0, {'commit': None, 'subject': 'pending shared-checkout integration'})
    cmake = (source / 'modules/c-api/CMakeLists.txt').read_text()
    executables = {m[1]: m[2] for m in re.finditer(r'add_executable\(\s*(\w+)\s+([^)]*)\)', cmake)}
    registrations = c_api_registrations(cmake)
    registry = (source / 'modules/graphics/examples/parity/ParityFixtures.cmake').read_text()
    fixtures = re.search(r'set\(CNA_CONFORMANCE_FIXTURES\s+(.*?)\n\)', registry, re.S)
    fixture_names = set(re.sub(r'#[^\n]*', '', fixtures[1]).split()) if fixtures else set()
    files = []
    for path, changes in sorted(owners.items()):
        if not ('/tests/' in path or '/Tests/' in path or path.endswith('_test.cpp') or
                '/conformance/' in path or 'sprite_depthstencil_test' in path or
                Path(path).name.startswith(('test_', 'test-')) or
                path.startswith(('tools/fna-reference/', 'tools/evidence/fixtures/'))):
            continue
        file = source / path
        if not file.is_file() or '__pycache__' in path:
            continue
        try:
            text = file.read_text()
        except UnicodeDecodeError:
            continue
        original = subprocess.run(['git', '-C', str(source), 'show', f'{base}:{path}'],
                                  text=True, capture_output=True)
        old = {d['name'] for d in definitions(original.stdout)}
        added = [d for d in definitions(text) if d['name'] not in old]
        workflow, reason = lane(path)
        targets = [name for name, body in executables.items() if path.removeprefix('modules/c-api/') in body]
        ctests = [{'name': name, 'command': command.strip()} for name, command in registrations
                  if any(command.strip().split()[0] == target for target in targets)]
        if path.startswith('modules/graphics/examples/conformance/') and path.endswith('_test.cpp'):
            fixture = Path(path).name.removesuffix('_test.cpp')
            ctests = [{'name': f'{prefix}_Conformance_{fixture}',
                       'command': 'renderer-owned conformance executable',
                       'selection': ('general-tests-ci.yml: unfiltered CTest' if prefix == 'EasyGL'
                                     else 'owner private renderer lane; not in current workflow selections')}
                      for prefix in ('EasyGL', 'SdlGpu', 'WebGPU', 'Fna3d', 'Software', 'Headless')
                      if fixture in fixture_names]
            if not ctests:
                workflow, reason = None, 'Source is absent from CNA_CONFORMANCE_FIXTURES; owner registration required.'
        if path.endswith('/webgpu_sprite_depthstencil_test.cpp'):
            ctests = [{'name': 'WebGPU_SpriteDepthStencil',
                       'command': 'cna_test_webgpu_sprite_depthstencil',
                       'selection': 'owner private WEBGPU lane; no workflow configures WEBGPU'}]
        if path.endswith('/spritebatch_subpixel_contract_test.cpp'):
            ctests = [{'name': f'{prefix}_SpriteBatchSubpixelContract',
                       'command': 'renderer-owned subpixel contract executable',
                       'selection': ('general-tests-ci.yml: unfiltered CTest' if prefix == 'EasyGL'
                                     else 'owner private renderer lane')}
                      for prefix in ('EasyGL', 'SdlGpu', 'WebGPU', 'Fna3d', 'Software', 'Headless')]
        files.append({'file': path, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                      'changes': changes, 'new_file': original.returncode != 0,
                      'added_definitions': added, 'ctests': ctests,
                      'ci_selection': workflow, 'limits': reason})
    mutants = source.parent / 'cnaplan/evidence/test011/mutants'
    mutation_witnesses = [{'patch': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest(),
                          'ci_selection': None, 'reason': 'Owner exclusive mutation run; never applied in normal CI'}
                         for p in sorted(mutants.glob('*.patch'))] if mutants.is_dir() else []
    return {'schema': 1, 'mutation_witnesses': mutation_witnesses,
            'base': base, 'head': head, 'dirty': dirty, 'files': files,
            'counting': 'Added macro/unittest definitions versus base, not expanded cases or member closure. '
                        'Modified existing tests and support fixtures remain per-file records. '
                        'CI selections are static workflow/registration review, not proof a remote run passed.',
            'commits': commits}


def markdown(data):
    lines = ['# Campaign test and CI inventory', '', data['counting'], '',
             f"Range: `{data['base']}` through `{data['head']}`, plus explicitly marked pending changes.", '',
             'The general job now enables the C ABI and runs recipe/coverage tool controls. '
             'Other selections were already present. Disabled cases, conditional modules, capability '
             'refusals and skips do not become passed assertions by appearing here.', '',
             '| File | Added definitions / CTest names | CI selection and limits |', '|---|---|---|']
    for file in data['files']:
        names = [f"`{d['name']}` ({d['kind']})" for d in file['added_definitions']]
        names += [f"`{c['name']}`" for c in file['ctests']]
        lines.append('| `' + file['file'] + '` | ' + ('<br>'.join(names) or 'Existing witness changed / supporting fixture') +
                     ' | ' + (file['ci_selection'] or '**No CI selection**') + '. ' + file['limits'] + ' |')
    lines += ['', 'Mutation witnesses: MTH-TEST-011 retained 40 individual patches in '
              '`evidence/test011/mutants/` and the original TEST-011 lane. They are deliberate '
              'owner-run mutations requiring exclusive source ownership; never applied in ordinary CI. '
              'The strengthened ownership regression runs normally via CnaTests.', '',
              *[f"- `{Path(m['patch']).name}` (SHA-256 `{m['sha256']}`)" for m in data['mutation_witnesses']], '',
              'Fresh FNA/XNA reference generation, GPU replay, WEBGPU, native device/hotplug, '
              'physical audio/service and foreign-platform qualification remain owner lanes. '
              'A CI declaration is not evidence those legs have run.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    data = inventory(args.source.resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / 'campaign-tests.json').write_text(json.dumps(data, indent=2) + '\n')
    (args.output_dir / 'CI_INVENTORY.md').write_text(markdown(data))


if __name__ == '__main__':
    main()
