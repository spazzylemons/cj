# wrap the test program and verify the roundtrip

import json
import subprocess
import sys

EXIT_CRASH = 2

try:
    roundtrip = subprocess.check_output(['./test', sys.argv[1]])
except subprocess.CalledProcessError as e:
    sys.exit(e.returncode)
except:
    sys.exit(EXIT_CRASH)

try:
    with open(sys.argv[1], 'rb') as file:
        v = json.load(file, parse_int=float)

    assert v == json.loads(roundtrip, parse_int=float)
except:
    sys.exit(EXIT_CRASH)
