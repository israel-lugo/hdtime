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

/* get size_t */
#include <stddef.h>

/* get strlen and snprintf */
#include <string.h>
#include <stdio.h>

/* get malloc */
#include <stdlib.h>

#if !defined(DEBUG) || !DEBUG
#  define NDEBUG 1
#endif
#include <assert.h>

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

static const char *const SECOND_FRACTION_UNITS[] = { "ns", "us", "ms", "s" };
#define NUM_SECOND_FRACTION_UNITS (sizeof(SECOND_FRACTION_UNITS)/sizeof(SECOND_FRACTION_UNITS[0]))


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


/* Used by split_time and other time-related functions */
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
 * Split an amount of time into its components.
 *
 * Receives an amount of time in nanoseconds, and returns a struct
 * human_time_value containing the individual fields (years, months, and so
 * on).
 */
struct human_time_value split_time(uint64_t nanoseconds)
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


/* Big enough to represent around 2**650 plus unit name */
#define STR_BUF_SIZE 256
struct time_string_bufs {
    char years[STR_BUF_SIZE];
    char months[STR_BUF_SIZE];
    char days[STR_BUF_SIZE];
    char hours[STR_BUF_SIZE];
    char minutes[STR_BUF_SIZE];
    char seconds_and_fractions[STR_BUF_SIZE];
};


/*
 * Convert a numeric value to a string, unless it is zero.
 *
 * Converts the numeric value to a string, with added units. Stores the
 * resulting string in the memory pointed-to by buf, which is of the
 * specified size.
 *
 * If the numeric value is zero, the string will be empty ("").
 *
 * If the buffer isn't big enough to hold the entire string (including the
 * null terminator), the string will be truncated.
 */
static void nonzero_value_to_str(char *buf, size_t size, unsigned int value,
        const char *unit)
{
    if (value)
        snprintf(buf, size, "%u %s", value, unit);
    else
        buf[0] = '\0';
}


/*
 * Join non-empty strings, with a separator.
 *
 * Receives an array of strings, the number of strings, and a string to be
 * used as separator.
 *
 * Allocates a new string using malloc() and stores it in the char *
 * pointed-to by joined. This string is the concatenation of the non-empty
 * strings in the array, with the specified separator.
 *
 * The new string should be freed when no longer necessary.
 *
 * Returns the number of non-empty strings that were joined (which may be
 * zero if all strings were empty), or a negative number in case of memory
 * error. If all strings were empty, or in case of error, the location
 * pointed-to by joined is set to NULL.
 */
int join_nonempty(char **joined, char *const strings[], int count, const char *sep)
{
    const size_t seplen = strlen(sep);
    size_t total_len = 0;
    int nonempty_count = 0;
    int i;
    char *new, *wptr;

    /* first pass, to count nonempty strings */
    for (i=0; i<count; i++)
    {
        if (strings[i][0] == '\0')
            continue;

        nonempty_count++;
        total_len += strlen(strings[i]);
    }

    if (nonempty_count == 0)
    {
        *joined = NULL;
        return 0;
    }

    /* add necessary space for N-1 separators */
    total_len += seplen*(nonempty_count-1);

    new = malloc(total_len + 1);
    if (new == NULL)
        return -1;

    /* second pass, to join strings */
    wptr = new;
    for (i=0; i<count; i++)
    {
        if (strings[i][0] == '\0')
            continue;

        wptr = stpcpy(wptr, strings[i]);
        wptr = stpcpy(wptr, sep);
    }

    assert(*wptr == '\0');

    *joined = new;

    return nonempty_count;
}


/*
 * Format a time value as a string.
 *
 * Receives a struct human_time_value, and returns a newly allocated string
 * containing its human-friendly representation. seconds_precision
 * specifies the number of decimal digits for the seconds and fractions of
 * seconds. This value is ignored if the seconds and fractions are only
 * composed of nanoseconds, as this function does not have sub-ns
 * precision.
 *
 * The string is allocated with malloc() and should freed when no longer
 * necessary.
 */
char *format_time_value(const struct human_time_value *v, int seconds_precision)
{
    static const char *sep = ", ";
    static struct time_string_bufs bufs;
    static char *const strings[] = {
        bufs.years, bufs.months, bufs.days, bufs.hours, bufs.minutes,
        bufs.seconds_and_fractions
    };
    uint64_t seconds_and_fractions_ns;
    long double seconds_and_fractions;
    const char *seconds_unit;
    char *str;

    nonzero_value_to_str(bufs.years, sizeof(bufs.years), v->years, "years");
    nonzero_value_to_str(bufs.months, sizeof(bufs.months), v->months, "months");
    nonzero_value_to_str(bufs.days, sizeof(bufs.days), v->days, "days");
    nonzero_value_to_str(bufs.hours, sizeof(bufs.hours), v->hours, "h");
    nonzero_value_to_str(bufs.minutes, sizeof(bufs.minutes), v->minutes, "m");

    seconds_and_fractions_ns = v->nanoseconds*SCALE_NS
        + v->microseconds*SCALE_US
        + v->miliseconds*SCALE_MS
        + v->seconds*SCALE_S;

    humanize_value(seconds_and_fractions_ns, 1000, SECOND_FRACTION_UNITS,
            NUM_SECOND_FRACTION_UNITS, &seconds_and_fractions,
            &seconds_unit);

    if (seconds_and_fractions_ns == v->nanoseconds)
    {   /* unit will be nanoseconds; we don't have sub-ns precision, so
           override precision argument to avoid lying about it */
        seconds_precision = 0;
    }

    snprintf(bufs.seconds_and_fractions, sizeof(bufs.seconds_and_fractions),
            "%.*Lf %s", seconds_precision, seconds_and_fractions, seconds_unit);

    (void)join_nonempty(&str, strings, sizeof(strings)/sizeof(strings[0]), sep);

    return str;
}


/*
 * Humanize an amount of time, which is given in nanoseconds.
 *
 * Receives an amount of time, in nanoseconds. Returns a newly allocated
 * string, with a human representation of the specified time interval.
 *
 * seconds_precision specifies the number of decimal digits for the seconds
 * and fractions of seconds. This value is ignored if the seconds and
 * fractions are only composed of nanoseconds, as this function does not
 * have sub-ns precision.
 *
 * The string is allocated with malloc(). It should be freed once no longer
 * necessary.
 */
char *humanize_time(uint64_t nanoseconds, int seconds_precision)
{
    struct human_time_value t = split_time(nanoseconds);

    return format_time_value(&t, seconds_precision);
}


/* vim: set expandtab smarttab shiftwidth=4 softtabstop=4 tw=75 : */
