#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# snapkitty-regex
# Copyright (C) 2026 SnapKitty Collective
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published
# by the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Pathological benchmark: a?^n a^n against a^n.

Cox's article shows Perl needing 60+ seconds for n=29 while the Thompson
NFA needs microseconds. Here we pit snapkitty-regex against Python's
backtracking `re` on the same input. Python runs are capped with a
timeout so the demo finishes.
"""
import subprocess
import sys
import time

BIN = "./regex"
TIMEOUT = 15


def time_ours(n):
    pat = "a?" * n + "a" * n
    txt = "a" * n
    t0 = time.perf_counter()
    r = subprocess.run([BIN, pat, txt], capture_output=True)
    dt = time.perf_counter() - t0
    assert r.returncode == 0, f"ours failed at n={n}"
    return dt


def time_pyre(n):
    code = (
        "import re, time; "
        f"rx = re.compile('a?'*{n} + 'a'*{n}); txt = 'a'*{n}; "
        "t0=time.perf_counter(); m=rx.fullmatch(txt); "
        "dt=time.perf_counter()-t0; "
        "assert m; print(f'{dt:.6f}')"
    )
    try:
        r = subprocess.run([sys.executable, "-c", code],
                           capture_output=True, text=True, timeout=TIMEOUT)
        return float(r.stdout.strip())
    except subprocess.TimeoutExpired:
        return None


def main():
    print(f"{'n':>5} {'snapkitty-regex':>16} {'python re':>16}")
    print("-" * 41)
    for n in (10, 15, 20, 22, 24, 26):
        ours = time_ours(n)
        py = time_pyre(n)
        py_s = f"{py:.6f}s" if py is not None else f">{TIMEOUT}s (timeout)"
        print(f"{n:>5} {ours:>15.6f}s {py_s:>16}")
    print()
    print("Scaling of snapkitty-regex alone (O(m*n), no blowup):")
    for n in (29, 50, 100, 200, 500):
        print(f"  n={n:<4} {time_ours(n):.6f}s")


if __name__ == "__main__":
    main()
