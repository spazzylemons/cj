import json
import subprocess

from pathlib import Path
from traceback import print_exc
from typing import Optional

def run_test_program(test_file: Path) -> Optional[bytes]:
    try:
        return subprocess.check_output(['./test', str(test_file)], timeout=5.0)
    except subprocess.CalledProcessError as e:
        # signal raised
        if e.returncode < 0:
            raise
        # failure occurred
        return None

def run_test(test_file: Path):
    roundtrip = run_test_program(test_file)

    match test_file.name[0]:
        case 'y':
            assert roundtrip is not None
        case 'n':
            assert roundtrip is None

    if roundtrip is None:
        return

    with test_file.open('rb') as file:
        v = json.load(file, parse_int=float)

    assert v == json.loads(roundtrip, parse_int=float)

# compile test program
subprocess.check_call(['cc', 'test.c', 'cj.o', '-o', 'test'])

# run tests and then delete test program
try:
    failures = 0
    test_dir = Path('tests')
    for test_file in test_dir.iterdir():
        try:
            run_test(test_file)
        except:
            print_exc()
            print('FAIL: {}'.format(test_file))
            failures += 1
    if failures != 0:
        print('{} tests failed'.format(failures))
        exit(1)
finally:
    Path('test').unlink()
