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


/* cli.c - command-line interface */


#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <getopt.h>

#include "benchmarks.h"
#include "humanize.h"


#define PACKAGE_NAME "hdtime"

#define PACKAGE_VERSION "0.1"

#define COPYRIGHT "Copyright (C) 2012 Israel G. Lugo"


/* Default value for cli_options.num_seeks, meaning autodetect. */
#define DEFAULT_NUM_SEEKS 0

/* Default value for cli_options.read_size, meaning autodetect. */
#define DEFAULT_SEQ_READ_BYTES 0


/* Program's basename, for printing on error. */
static const char *prog_name = NULL;


struct cli_options {
    const char *devname;
    unsigned int num_seeks;
    size_t read_size;
};


/*
 * Show command-line options with pretty formatting.
 *
 * To be used from within show_usage.
 */
static void show_options(void)
{
    static const struct { const char *name; const char *desc; } opts[] = {
        { "-c, --read-count=N", "do N random reads in the seek test" },
        { "", "(default: autodetect)" },
        { "-s, --read-size=SIZE", "size of read blocks in the sequential test" },
        { "", "(default: autodetect)" },
        { "-h, --help", "display this help and exit" },
        { "-v, --version", "output version information and exit" },
    };

    unsigned int i;
    for (i=0; i < sizeof(opts)/sizeof(opts[0]); i++)
    {
        printf("  %-28s%s\n", opts[i].name, opts[i].desc);
    }
}


/*
 * Show usage information.
 */
static void show_usage(void)
{
    printf("%s (%s) %s - measure block device performance\n"
           "%s\n"
           "\n"
           "This program does read tests on a block device, such as a hard drive,\n"
           "and provides several timing values for benchmark and comparison purposes.\n"
           "All tests are read-only; any data on the device is left untouched.\n"
           "\n",
           prog_name,
           PACKAGE_NAME,
           PACKAGE_VERSION,
           COPYRIGHT);
    printf(" Usage:\n"
           "  %s [OPTIONS] <device>\n"
           "\n"
           "\n"
           "OPTIONS:\n",
           prog_name);
    show_options();
    printf("\n"
           "The SIZE value can be suffixed with an optional unit: KiB, MiB, GiB\n"
           "TiB, PiB, EiB, ZiB, YiB (powers of 1024), or KB, MB, GB, TB, PB, EB,\n"
           "ZB, YB (powers of 1000). K, M, G, T, P, E, Z, Y are also accepted, as\n"
           "powers of 1024.\n");
}


/*
 * Show version information.
 */
static void show_version_info(void)
{
    printf("%s %s\n"
           "%s\n"
           "License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>\n"
           "This is free software: you are free to change and redistribute it.\n"
           "There is NO WARRANTY, to the extent permitted by law.\n",
           PACKAGE_NAME,
           PACKAGE_VERSION,
           COPYRIGHT);
}


/*
 * Print a string directing the user to the help functionality.
 *
 * To be used whenever the user inputs an invalid argument.
 */
static void print_help_string(void)
{
    fprintf(stderr, "Try '%s --help' for more information.\n", prog_name);
}


/*
 * Get an uintmax_t from a string argument.
 *
 * Parses an option argument from string arg, to retrieve an uintmax_t in
 * base 10. The value is verified to be between min and max.
 *
 * arg_name must be a string description of the option for error message
 * purposes, e.g. "read block size". help_func, if non-NULL, must be a
 * pointer to a help function, which will be called after printing the
 * error message.
 *
 * If the argument is invalid (out of bounds or not an unsigned integer),
 * the function prints an error, calls help_func() and exits the
 * program.
 */
static uintmax_t get_uint_arg(const char *arg, uintmax_t min,
        uintmax_t max, const char *arg_name, void (*help_func)(void))
{
	char *end;
        uintmax_t result;

        /* set errno to distinguish between overflow and actual UINTMAX_MAX */
        errno = 0;
	result = strtoumax(arg, &end, 10);

	if (result < min
            || result > max
            || *end != '\0'
            || (result == UINTMAX_MAX && errno == ERANGE))
	{
		fprintf(stderr,
			"%s: invalid %s given (%" PRIuMAX "..%" PRIuMAX ")\n",
			prog_name, arg_name, min, max);
                if (help_func != NULL)
                    help_func();
		exit(1);
	}
	return result;
}



/*
 * Process command-line arguments.
 *
 * Receives the number of arguments (argc), the array of arguments (argv)
 * and a pointer to a struct cli_options which will be modified as per the
 * specified command-line arguments. The struct cli_options should already
 * be initialized to safe defaults. Exits on error.
 */
static void parse_args(int argc, char *const argv[],
        struct cli_options *p_cli_options)
{
    static const struct option long_opts[] = {
        {"read-count", 1, 0, 'c'},
        {"read-size", 1, 0, 's'},
        {"help", 0, 0, 'h'},
        {"version", 0, 0, 'v'},
        {0, 0, 0, 0}
    };

    /* initialize defaults */
    p_cli_options->num_seeks = DEFAULT_NUM_SEEKS;
    p_cli_options->read_size = DEFAULT_SEQ_READ_BYTES;

    for (;;)
    {
        int arg = getopt_long(argc, argv, "c:s:hv", long_opts, NULL);
        int status;

        if (arg == -1)
            break;

        switch (arg)
        {
            case 'c':   /* --read-count <n> */
                p_cli_options->num_seeks = (unsigned int)get_uint_arg(optarg,
                        1, UINT_MAX, "read count", print_help_string);
                break;
            case 's':   /* --read-size <size> */
                status = parse_human_size(optarg, &p_cli_options->read_size);

                if (status != 0 || p_cli_options->read_size == 0)
                {   /* error, or invalid size 0 specified */
                    fprintf(stderr,
                            "%s: invalid read block size given (1..%" PRIuMAX " bytes)\n",
                            prog_name, SIZE_MAX);
                    print_help_string();
                    exit(1);
                }
                break;
            case 'h':   /* --help */
                show_usage();
                exit(0);
            case 'v':   /* --version */
                show_version_info();
                exit(0);
            default:    /* invalid option or missing mandatory argument */
                print_help_string();
                exit(2);
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "%s: missing device name\n", prog_name);
        print_help_string();
        exit(2);
    }

    p_cli_options->devname = argv[optind];
}


int main(int argc, char *argv[])
{
    struct cli_options cli_options;

    prog_name = basename(argv[0]);

    parse_args(argc, argv, &cli_options);

    run_and_print_benchmarks(cli_options.devname, cli_options.num_seeks,
            cli_options.read_size);

    exit(0);
}

/* vim: set expandtab smarttab shiftwidth=4 softtabstop=4 tw=75 : */
