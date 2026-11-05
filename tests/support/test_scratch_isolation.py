#!/usr/bin/env python3
# SPDX-License-Identifier: MS-PL
"""Exercise real competing processes and normal/failing process cleanup, without a window."""
import concurrent.futures
import json
import os
import pathlib
import subprocess
import sys


def main():
    executable, source = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve()
    working = pathlib.Path(os.environ.get('CNA_TEST_SCRATCH_PROBE_CWD', str(executable.parent))).resolve()
    # Unrelated cwd also catches a helper accidentally rooted at the caller's working directory.
    processes = [subprocess.Popen([str(executable)], cwd=working, text=True, encoding='utf-8',
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE) for _ in range(2)]
    roots = []
    try:
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            reads = [pool.submit(p.stdout.readline) for p in processes]
            roots = [pathlib.Path(f.result(timeout=20).strip()) for f in reads]
        if os.environ.get('CNA_TEST_SCRATCH_TRACE'):
            print('scratch-roots:' + json.dumps([str(root) for root in roots]), flush=True)
        assert all(root.is_absolute() for root in roots), roots
        assert roots[0] != roots[1], roots
        for root in roots:
            assert root != source and source not in root.parents, root
            assert (root / 'same-name.bin').read_bytes() == b'scratch-oracle'
        # One child fails; its fixture owner must still clean up. The peer remains live.
        processes[0].stdin.write('f\n')
        processes[0].stdin.flush()
        assert processes[0].wait(timeout=20) == 23
        assert not roots[0].exists(), roots[0]
        assert (roots[1] / 'same-name.bin').read_bytes() == b'scratch-oracle'
        processes[1].stdin.write('s\n')
        processes[1].stdin.flush()
        assert processes[1].wait(timeout=20) == 0
        assert not roots[1].exists(), roots[1]
    finally:
        for p in processes:
            if p.poll() is None:
                p.communicate('s\n', timeout=20)

    environment = dict(os.environ, TMPDIR=str(source), TMP=str(source), TEMP=str(source))
    refused = subprocess.run([str(executable)], input='f\n', text=True, encoding='utf-8', capture_output=True,
                             cwd=working, env=environment, timeout=20)
    assert refused.returncode == 2, (refused.returncode, refused.stdout, refused.stderr)
    assert refused.stdout == '', refused.stdout


if __name__ == '__main__':
    main()
