/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 *
 * snapkitty-regex
 * Copyright (C) 2026 SnapKitty Collective
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* snapkitty-regex CLI: match patterns against strings. */
#include "regex.h"

#include <stdio.h>
#include <string.h>

static void
usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-s] <pattern> <string>...\n", prog);
	fprintf(stderr, "  -s  search (unanchored) instead of full match\n");
}

int
main(int argc, char **argv)
{
	int search = 0;
	int ai = 1;
	int allmatch = 1;
	Regex *re;

	if (ai < argc && strcmp(argv[ai], "-s") == 0) {
		search = 1;
		ai++;
	}
	if (argc - ai < 2) {
		usage(argv[0]);
		return 2;
	}
	re = re_compile(argv[ai++]);
	if (re == NULL) {
		fprintf(stderr, "error: bad pattern\n");
		return 2;
	}
	for (; ai < argc; ai++) {
		int m = search ? re_search(re, argv[ai])
			       : re_fullmatch(re, argv[ai]);
		printf("%s: %s\n", argv[ai], m ? "MATCH" : "NO MATCH");
		if (!m)
			allmatch = 0;
	}
	re_free(re);
	return allmatch ? 0 : 1;
}
