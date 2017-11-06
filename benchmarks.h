/*
 * hdtime - performance measurements for block devices
 * Copyright (C) 2012 Israel G. Lugo
 *
 * hdtime is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * hdtime is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with hdtime.  If not, see <http://www.gnu.org/licenses/>.
 *
 * For suggestions, feedback or bug reports: israel.lugo@lugosys.com
 */


/* benchmarks.h - benchmark running module header */


#ifndef _BENCHMARKS_H
#define _BENCHMARKS_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif


/* get size_t */
#include <stddef.h>

void run_and_print_benchmarks(const char *devname, unsigned int num_seeks,
        size_t read_size);


#endif  /* _BENCHMARKS_H */
