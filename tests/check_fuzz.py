# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""The tiny harness alone cannot cover 100 edges; the reader must be instrumented."""
import re
import subprocess
import sys

result = subprocess.run([sys.argv[1], '-runs=0', sys.argv[2]],
                        capture_output=True, text=True, check=True)
coverage = re.findall(r'cov: (\d+)', result.stderr)
assert coverage and max(map(int, coverage)) > 100, result.stderr
print('parser coverage instrumentation: OK')
