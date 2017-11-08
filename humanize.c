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


/* humanize.c - humanize values module */


#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdint.h>
#include <math.h>
#include <float.h>


#include "humanize.h"


#define min(x, y) ({  \
  __typeof__(x) _x = (x); \
  __typeof__(y) _y = (y); \
  _x < _y ? _x : _y;  \
})

#define max(x, y) ({  \
  __typeof__(x) _x = (x); \
  __typeof__(y) _y = (y); \
  _x > _y ? _x : _y;  \
})


static const char *const BINARY_IEC_UNITS[] = { "B", "KiB", "MiB", "GiB", "TiB",
    "PiB", "EiB", "ZiB", "YiB" };
#define NUM_BINARY_IEC_UNITS (sizeof(BINARY_IEC_UNITS)/sizeof(BINARY_IEC_UNITS[0]))

static const char *const SPEED_IEC_UNITS[] = { "B/s", "KiB/s", "MiB/s", "GiB/s",
    "TiB/s", "PiB/s", "EiB/s", "ZiB/s", "YiB/s" };
#define NUM_SPEED_IEC_UNITS (sizeof(SPEED_IEC_UNITS)/sizeof(SPEED_IEC_UNITS[0]))


/*
 * Humanize an unsigned integer value into a long double of a certain unit.
 *
 * Receives the value (a 64-bit unsigned int), the geometric ratio between
 * each unit (e.g. 1024 for byte -> kbyte -> mbyte), an array of unit
 * names and the length of said array.
 *
 * This function divides x by ratio (moving to the next larger unit) until
 * it can be divided no further, or until it runs out of available units.
 *
 * Returns the humanized value in the long double pointed-to by result, and
 * the name of the unit in the const char * pointed-to by unit. The unit is
 * an element selected from the specified array of units.
 */
void humanize_value(uint64_t x, unsigned int ratio, const char *const units[],
        unsigned int num_units, long double *result, const char **unit)
{
    unsigned int unit_exp = 0;
    long double i;

    for (i=x; i>=ratio && unit_exp<num_units; i/=ratio)
    {
        unit_exp++;
    }

    *result = i;
    *unit = units[unit_exp];
}


/*
 * Humanize a size which is given in bytes.
 *
 * Receives the size in bytes. Returns a struct human_value containing the
 * humanized size value and its unit.
 *
 * The unit string is statically allocated and must not be freed nor
 * modified in any way.
 */
struct human_value humanize_binary_size(uint64_t x)
{
    struct human_value result;

    humanize_value(x, 1024, BINARY_IEC_UNITS, NUM_BINARY_IEC_UNITS,
            &result.value, &result.unit);

    return result;
}


/*
 * Humanize a speed which is given in bytes.
 *
 * Receives the speed in bytes. Returns a struct human_value containing the
 * humanized speed value and its unit.
 *
 * The unit string is statically allocated and must not be freed nor
 * modified in any way.
 */
struct human_value humanize_binary_speed(uint64_t x)
{
    struct human_value result;

    humanize_value(x, 1024, SPEED_IEC_UNITS, NUM_SPEED_IEC_UNITS,
            &result.value, &result.unit);

    return result;
}


/* TODO: Humanize time values. */


/* vim: set expandtab smarttab shiftwidth=4 softtabstop=4 tw=75 : */
