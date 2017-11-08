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


#ifndef _HUMANIZE_H
#define _HUMANIZE_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif


/* get uint64_t */
#include <stdint.h>


struct human_value {
    long double value;
    const char *unit;
};


void humanize_value(uint64_t x, unsigned int ratio, const char *const *units,
        unsigned int num_units, long double *result, const char **unit);

struct human_value humanize_binary_size(uint64_t x);

struct human_value humanize_binary_speed(uint64_t x);

#endif  /* _HUMANIZE_H */
