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


/* Used by humanize_time */
#define SCALE_NS 1ULL
#define SCALE_US (1000*SCALE_NS)
#define SCALE_MS (1000*SCALE_US)
#define SCALE_S (1000*SCALE_MS)
#define SCALE_MIN (60*SCALE_S)
#define SCALE_HOUR (60*SCALE_MIN)
#define SCALE_DAY (24*SCALE_HOUR)
#define SCALE_YEAR (365*SCALE_DAY)
#define SCALE_MONTH (SCALE_YEAR/12)


/*
 * Humanize an amount of time, which is given in nanoseconds.
 *
 * Receives an amount of nanoseconds, and returns a strut human_time_value
 * containing the individual fields (years, months, and so on).
 */
struct human_time_value humanize_time(uint64_t nanoseconds)
{
    struct human_time_value v;

    v.years = nanoseconds / SCALE_YEAR;
    const uint64_t without_years = nanoseconds % SCALE_YEAR;

    v.months = without_years / SCALE_MONTH;
    const uint64_t without_months = without_years % SCALE_MONTH;

    v.days = without_months / SCALE_DAY;
    const uint64_t without_days = without_months % SCALE_DAY;

    v.hours = without_days / SCALE_HOUR;
    const uint64_t without_hours = without_days % SCALE_HOUR;

    v.minutes = without_hours / SCALE_MIN;
    const uint64_t without_minutes = without_hours % SCALE_MIN;

    v.seconds = without_minutes / SCALE_S;
    const uint64_t without_seconds = without_minutes % SCALE_S;

    v.miliseconds = without_seconds / SCALE_MS;
    const uint64_t without_miliseconds = without_seconds % SCALE_MS;

    v.microseconds = without_miliseconds / SCALE_US;
    const uint64_t without_microseconds = without_miliseconds % SCALE_US;

    v.nanoseconds = without_microseconds / SCALE_NS;

    return v;
}


/* vim: set expandtab smarttab shiftwidth=4 softtabstop=4 tw=75 : */
