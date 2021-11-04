from JSONTestSuite.run_tests import programs, run_tests

import os
import subprocess
import sys

# add entry for our library
programs['C cj'] = {
    'url': 'https://github.com/spazzylemons/cj',
    'commands': ['./test'],
}

# compile test program
if subprocess.call(['cc', 'test.c', 'cj.o', '-o', 'test']) != 0:
    print('could not compile test program, tests cannot be performed')
    sys.exit(1)

# run tests and then delete test program
try:
    run_tests(restrict_to_program='["C cj"]')
finally:
    os.remove('test')
