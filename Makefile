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

CC = cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -Wpedantic
PREFIX ?= /usr/local

all: regex

regex: src/regex.c src/main.c src/regex.h
	$(CC) $(CFLAGS) -o $@ src/regex.c src/main.c

test: regex
	python3 tests/test_regex.py

bench: regex
	python3 tests/bench.py

clean:
	rm -f regex

install: regex
	install -m 755 regex $(PREFIX)/bin/regex

.PHONY: all test bench clean install
