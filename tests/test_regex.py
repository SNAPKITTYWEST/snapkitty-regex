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

"""Correctness harness for snapkitty-regex.

Runs the compiled CLI against a corpus of (pattern, text, expected)
cases for full-match, search, and malformed-pattern rejection.
Exit 0 only if every case passes.
"""
import subprocess
import sys

BIN = "./regex"

# (pattern, text, expected_fullmatch)
FULLMATCH_CASES = [
    # literals & concatenation
    ("abc", "abc", True),
    ("abc", "abd", False),
    ("abc", "ab", False),
    ("abc", "abcd", False),
    # alternation
    ("a|b", "a", True),
    ("a|b", "b", True),
    ("a|b", "c", False),
    ("ab|cd", "ab", True),
    ("ab|cd", "cd", True),
    ("ab|cd", "abcd", False),
    ("ab|cd|ef", "ef", True),
    # star / plus / question
    ("a*", "", True),
    ("a*", "a", True),
    ("a*", "aaaaa", True),
    ("a*", "b", False),
    ("a*", "aaab", False),
    ("a+", "", False),
    ("a+", "a", True),
    ("a+", "aaaa", True),
    ("a?", "", True),
    ("a?", "a", True),
    ("a?", "aa", False),
    # grouping & nesting
    ("(ab)+", "ab", True),
    ("(ab)+", "ababab", True),
    ("(ab)+", "aba", False),
    ("(ab)*", "", True),
    ("(a|b)*c", "c", True),
    ("(a|b)*c", "ababc", True),
    ("(a|b)*c", "ababd", False),
    ("((a|b)(c|d))*", "", True),
    ("((a|b)(c|d))*", "acbd", True),
    ("((a|b)(c|d))*", "acb", False),
    # Cox's worked examples
    ("a(bb)+a", "abba", True),
    ("a(bb)+a", "abbbba", True),
    ("a(bb)+a", "abbba", False),
    ("a?a?a?aaa", "aaa", True),
    ("a?a?a?aaa", "aa", False),
    # backslash escapes
    (r"a\*b", "a*b", True),
    (r"a\*b", "aaab", False),
    (r"a\+b", "a+b", True),
    (r"a\?b", "a?b", True),
    (r"a\|b", "a|b", True),
    (r"\(a\)", "(a)", True),
    (r"\(a\)", "a", False),
    (r"a\\b", "a\\b", True),
    ("a\\.b", "a.b", True),
    ("a\\.b", "axb", False),
    # mixed realistic patterns
    ("colou?r", "color", True),
    ("colou?r", "colour", True),
    ("colou?r", "colouur", False),
    ("(ab|cd)+e", "abe", True),
    ("(ab|cd)+e", "cdabe", True),
    ("(ab|cd)+e", "abcdcde", True),
    ("(ab|cd)+e", "e", False),
    ("0(1|2)*3", "03", True),
    ("0(1|2)*3", "012213", True),
    ("0(1|2)*3", "013", True),
    ("0(1|2)*3", "014", False),
    ("https?://a+", "http://aaa", True),
    ("https?://a+", "https://a", True),
    ("https?://a+", "ftp://a", False),
]

# (pattern, text, expected_search)
SEARCH_CASES = [
    ("b", "abc", True),
    ("b", "ac", False),
    ("a+", "xxaaaxx", True),
    ("(ab)+", "xxababxx", True),
    ("z", "abc", False),
    ("a(bb)+a", "xxabbbbaxx", True),
    ("colou?r", "the colour blue", True),
]

# patterns the compiler must reject (exit code 2)
BAD_PATTERNS = [
    "",
    "*",
    "+",
    "?",
    "(a",
    "a)",
    "()",
    "(a|)",
    "a|",
    "|a",
    "(a||b)",
    "a**b(",
    "\\",       # trailing backslash
    "(a\\",     # trailing backslash inside group
]


def run(args):
    return subprocess.run([BIN] + args, capture_output=True, text=True)


def main():
    failures = 0

    for pat, text, expected in FULLMATCH_CASES:
        r = run([pat, text])
        got = r.returncode == 0
        want_word = "MATCH" if expected else "NO MATCH"
        ok = got == expected and want_word in r.stdout and r.returncode in (0, 1)
        if not ok:
            failures += 1
            print(f"FAIL fullmatch: pattern={pat!r} text={text!r} "
                  f"expected={expected} rc={r.returncode} out={r.stdout.strip()!r}")

    for pat, text, expected in SEARCH_CASES:
        r = run(["-s", pat, text])
        got = r.returncode == 0
        if got != expected or r.returncode not in (0, 1):
            failures += 1
            print(f"FAIL search: pattern={pat!r} text={text!r} "
                  f"expected={expected} rc={r.returncode} out={r.stdout.strip()!r}")

    for pat in BAD_PATTERNS:
        r = run([pat, "x"])
        if r.returncode != 2:
            failures += 1
            print(f"FAIL badpattern: pattern={pat!r} expected rc=2, got rc={r.returncode}")

    total = len(FULLMATCH_CASES) + len(SEARCH_CASES) + len(BAD_PATTERNS)
    print(f"{total - failures}/{total} checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
