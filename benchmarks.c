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


/* benchmarks.c - benchmark-running module */


#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <linux/fs.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <stdint.h>
#include <inttypes.h>

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

/* CLOCK_MONOTONIC_RAW is immune to incremental adjustments performed by
 * adjtime() or NTP; however, it is Linux-specific */
#ifndef CLOCK_MONOTONIC_RAW
#  define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif


#define MIB (1024UL * 1024UL)

/* Default amount of random reads to do in the seek test. */
#define DEFAULT_RAND_READ_SEEKS 200

/* Minimum amount of nanoseconds to spend in the random access test. */
#define MIN_AUTO_RAND_READ_NS (1000000000UL)

/* Maximum amount of random reads to do in seek test when autodetecting. */
#define MAX_AUTO_RAND_READ_SEEKS 25600

/* Default amount of bytes to read sequentially in a single block. */
#define DEFAULT_SEQ_READ_BYTES (64 * MIB)

/* Minimum amount of nanoseconds to spend in the sequential read test. */
#define MIN_AUTO_SEQ_READ_NS (2000000000UL)

/* Maximum value of read_size in sequential reads when autodetecting. */
#define MAX_AUTO_SEQ_READ_BYTES (1024UL * MIB)


struct blkdev_info {
    uint64_t dev_size;
    uint64_t num_blocks;
    unsigned int block_size;
    size_t alignment;
};


struct benchmark_results {
    char *path;
    struct blkdev_info dev_info;
    size_t seq_read_bytes;
    unsigned int num_seeks;
    uint64_t seq_read_ns;
    uint64_t block_read_ns;
    uint64_t total_randaccess_ns;
    uint64_t randaccess_reading_ns;
    uint64_t seek_ns;
};



/*
 * Terminate the program if error is true.
 *
 * Prints the message to stderr before terminating, followed by a message
 * describing the specified error number.
 */
static inline void die_if_with_errno(int error, const char *msg, int errnum)
{
    if (error) {
        fprintf(stderr, "%s: %s\n", msg, strerror(errnum));
        exit(EXIT_FAILURE);
    }
}



/*
 * Terminate the program if error is true.
 *
 * Prints the message to stderr before terminating, followed by a message
 * describing the current value in errno.
 */
static inline void die_if(int error, const char *msg)
{
    if (error) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}



static inline size_t align_ceil(size_t n, size_t alignment)
{
    const size_t remainder = n % alignment;

    return remainder != 0 ? (n - remainder) + alignment : n;
}



/*
 * 64-bit version of random().
 *
 * Returns a random unsigned 64-bit number. Uses random() and scales it
 * proportionally up to 0..2**64-1.
 */
static inline uint64_t random64(void)
{
    if (RAND_MAX < ~(uint64_t)0)
        return random() * (~(uint64_t)0 / RAND_MAX);
    else
        /* cover system whose RAND_MAX (long int) doesn't fit in 64 bits */
        return (uint64_t)random();
}



/*
 * Return the logarithm of x to base 2, rounded down to zero.
 *
 * x must not be zero.
 */
static unsigned int log2_floor(unsigned int x)
{
    unsigned int exp = 0;

    assert(x != 0);

    while (x > 1)
    {
        exp++;
        x >>= 1;
    }

    assert(exp <= sizeof(x) * CHAR_BIT);

    return exp;
}



/*
 * Return the smallest power of 2 larger than or equal to x.
 *
 * Exits if x is larger than the largest power of 2 that will fit in an
 * unsigned int.
 */
static unsigned int smallest_power_of_2_that_holds(unsigned int x)
{
    unsigned int exp;

    if (x == 0)
        /* can't calculate log(0); just return the correct result (first power
         * of 2, 2**0 = 1) */
        return 1;

    /* make sure x fits in the largest power of 2 an unsigned int can hold */
    if (x > (1U << (sizeof(unsigned int)*CHAR_BIT - 1)))
    {
        fprintf(stderr,
                "error: %u doesn't fit in largest power of 2 an unsigned int can hold\n",
                x);
        exit(1);
    }

    exp = log2_floor(x);

    return 1U << exp == x ? x : 1U << (exp + 1);
}



/*
 * Convert a struct timespec to nanoseconds.
 *
 * May return undefined results due to integer overflow, if the seconds field
 * contains a value greater than (2**64 - 1) / 10**9 ~= 2**34 s ~= 584 years.
 */
static inline uint64_t timespec_to_ns(const struct timespec *ts)
{
    return (uint64_t)ts->tv_sec*1000000000UL + ts->tv_nsec;
}



/*
 * Calculate difference between two struct timespec, in nanoseconds.
 *
 * Returns the difference in nanoseconds between time t1 and time t0,
 * represented as an uint64_t. May have undefined behavior if t0 is greater
 * than t1, due to integer overflow.
 */
static uint64_t timespec_diff_ns(const struct timespec *t1,
        const struct timespec *t0)
{
    return timespec_to_ns(t1) - timespec_to_ns(t0);
}



/*
 * Get a device's physical block size. Receives an open file descriptor for the
 * device. Exits in case of error.
 */
static unsigned int get_physical_block_size(int fd)
{
    unsigned int block_size;
    int retval;

    retval = ioctl(fd, BLKPBSZGET, &block_size);
    die_if(retval == -1, "ioctl(BLKPBSZGET)");

    return block_size;
}



/*
 * Get buffer alignment for reading from fd.
 *
 * Uses the POSIX fpathconf interface to query proper alignment from the
 * system. In case of error or unspecified alignment, assumes fd is a block
 * device and falls back to checking its block size.
 */
static size_t get_readbuf_align(int fd)
{
    long align_l;

    errno = 0;
    align_l = fpathconf(fd, _PC_REC_XFER_ALIGN);

    assert(align_l >= -1);

    /* fallback to device's block size in case of error or useless value */
    switch (align_l)
    {
        case -1:    /* no specific align recommendation, or error */
            die_if(errno != 0, "fpathconf");
            /* FALL THROUGH */
        case 0:     /* 0 align makes no sense */
            align_l = (long)get_physical_block_size(fd);
    }

    return (size_t)align_l;
}



/*
 * Get the size of a device. Receives an open file descriptor for the device.
 * Exits in case of error.
 */
static uint64_t get_dev_size(int fd)
{
    uint64_t size;
    int retval;

    retval = ioctl(fd, BLKGETSIZE64, &size);
    die_if(retval == -1, "ioctl(BLKGETSIZE64)");

    return size;
}



/*
 * Wrapper for read() that does an lseek first. Exits in case of error.
 */
static void read_at(int fd, void *buffer, size_t count, off64_t offset)
{
    off64_t seek_ok;
    ssize_t read_ok;

    seek_ok = lseek64(fd, offset, SEEK_SET);
    die_if(seek_ok == (off64_t)-1, "lseek64");
    read_ok = read(fd, buffer, count);
    die_if(read_ok < 0, "read");
}



/*
 * Get a timestamp in seconds, with very high precision.
 *
 * Receives a pointer to a struct timespec, where the timestamp will be stored.
 * The time value is relative to some unspecified starting point; useful for
 * relative time calculations (timing measurements).
 */
static void get_cur_timestamp(struct timespec *now)
{
    int retval;

    retval = clock_gettime(CLOCK_MONOTONIC_RAW, now);
    die_if(retval == -1, "clock_gettime");
}



/*
 * Calculate the tolerance in taking two time measurements and calculating the
 * time delta. Returns half the maximum error in nanoseconds; the actual
 * tolerance is +/- the returned value.
 */
static uint64_t get_timing_tolerance_ns(void)
{
    struct timespec res;
    int retval;
    struct timespec t0, t1;
    uint64_t resolution_ns, delta_ns;

    /* get the underlying clock's resolution (lower bound) */
    retval = clock_getres(CLOCK_MONOTONIC_RAW, &res);
    die_if(retval == -1, "clock_getres");

    resolution_ns = timespec_to_ns(&res);

    /* measure the actual overhead of measuring time */
    get_cur_timestamp(&t0);
    get_cur_timestamp(&t1);
    delta_ns = timespec_diff_ns(&t1, &t0);

    /* tolerance is +/- half the maximum error */
    return max(resolution_ns, delta_ns) / 2;
}



/*
 * Allocate a block of memory of the specified size, aligned to the specified
 * alignment. Returns a pointer to the newly allocated memory. The memory
 * should be released with free() when no longer necessary. Exits in case of
 * error.
 */
static void *allocate_aligned_memory(size_t alignment, size_t size)
{
    void *buffer;
    int retval;

    retval = posix_memalign(&buffer,
                            smallest_power_of_2_that_holds(alignment),
                            size);
    die_if_with_errno(retval != 0, "posix_memalign", retval);

    return buffer;
}



/*
 * Get a block device's average block read time, for a given read size.
 *
 * Does a sequencial read test on a block device, to find its average read
 * speed. Receives the file descriptor of the block device, the desired read
 * size, and a pointer to a struct with information about the device.
 *
 * The desired read size must be greater than zero. It will be rounded up
 * to the nearest multiple of blkdev_info->alignment.
 *
 * Two sequential read operations will be performed; one at the logical
 * beginning of the device, and one at the logical end.
 *
 * p_total_bytes, if non-NULL, should point to a size_t which will be set
 * to the total amount of bytes read from the device. p_total_read_ns, if
 * non-NULL, should point to a uint64_t which will be set to the amount of
 * time that was spent reading, in nanoseconds.
 *
 * Returns the average time it takes to read a single block of the device, in
 * nanoseconds. Exits in case of error.
 */
static uint64_t get_block_read_for_size(int fd,
        const struct blkdev_info *blkdev_info, size_t read_size,
        size_t *p_total_bytes, uint64_t *p_total_read_ns)
{
    size_t aligned_read_size = align_ceil(read_size, blkdev_info->alignment);
    char *buffer;
    struct timespec start, end;
    uint64_t delta_ns;

    assert(aligned_read_size % blkdev_info->alignment == 0);
    assert(read_size != 0 && aligned_read_size != 0);

    if (aligned_read_size > blkdev_info->dev_size)
    {
        aligned_read_size = blkdev_info->dev_size;
    }

    const struct human_value total_read = humanize_binary_size(aligned_read_size*2);

    /* two reads: beginning and end of the device */
    printf("Reading %.2Lf %s to determine sequential read time, please wait...\n",
           total_read.value, total_read.unit);

    buffer = allocate_aligned_memory(blkdev_info->alignment, aligned_read_size);

    get_cur_timestamp(&start);
    read_at(fd, buffer, aligned_read_size, 0);
    read_at(fd, buffer, aligned_read_size, blkdev_info->dev_size - aligned_read_size);
    get_cur_timestamp(&end);

    free(buffer);

    delta_ns = timespec_diff_ns(&end, &start);

    if (p_total_bytes != NULL)
        *p_total_bytes = aligned_read_size * 2;

    if (p_total_read_ns != NULL)
        *p_total_read_ns = delta_ns;

    return delta_ns / ((aligned_read_size * 2) / blkdev_info->block_size);
}



/*
 * Get a block device's average block read time.
 *
 * Does sequencial read tests on a block device, to find its average read
 * speed. Receives the file descriptor of the block device, the (optional)
 * desired read size, and a pointer to a struct with information about the
 * device.
 *
 * If a non-zero read_size is specified, it will be rounded up to the
 * nearest multiple of blkdev_info->alignment, and used for a single
 * sequential read test.
 *
 * If read_size is zero, an appropriate read size will be autodetected, by
 * performing multiple sequential read tests of exponentially increasing
 * read sizes, until one takes at least MIN_AUTO_SEQ_READ_NS nanoseconds to
 * complete.
 *
 * The sequential read tests are done by calling get_block_read_for_size.
 *
 * p_total_bytes, if non-NULL, should point to a size_t which will be set
 * to the total amount of bytes read from the device, in all read test(s).
 * p_total_read_ns, if non-NULL, should point to a uint64_t which will be
 * set to the total amount of time that was spent reading, in nanoseconds.
 *
 * Returns the average time it takes to read a single block of the device, in
 * nanoseconds. Exits in case of error.
 */
static uint64_t get_block_read_ns(int fd, const struct blkdev_info *blkdev_info,
        size_t read_size, size_t *p_total_bytes, uint64_t *p_total_read_ns)
{
    uint64_t block_read_ns;

    if (read_size == 0)
    {   /* autodetect read size */
        size_t total_bytes = 0;
        uint64_t total_read_ns = 0;

        /* loop increasing read size until we take at least a certain amount
         * of time doing the read; keep track of total time and bytes read */
        for (read_size = DEFAULT_SEQ_READ_BYTES;
             total_read_ns < MIN_AUTO_SEQ_READ_NS && read_size <= MAX_AUTO_SEQ_READ_BYTES;
             read_size *= 2)
        {
            size_t _total_bytes;
            uint64_t _total_read_ns;

            /* ignore the return value, we calculate it later */
            (void)get_block_read_for_size(fd, blkdev_info, read_size,
                    &_total_bytes, &_total_read_ns);

            total_bytes += _total_bytes;
            total_read_ns += _total_read_ns;
        }

        /* calculate the time it takes to read one block, based on the
         * average time it took to make all automated reads */
        block_read_ns = total_read_ns / (total_bytes / blkdev_info->block_size);

        if (p_total_bytes != NULL)
            *p_total_bytes = total_bytes;
        if (p_total_read_ns != NULL)
            *p_total_read_ns = total_read_ns;
    }
    else
    {   /* use specified read size */
        block_read_ns = get_block_read_for_size(fd, blkdev_info, read_size,
                p_total_bytes, p_total_read_ns);
    }

    return block_read_ns;
}


/*
 * Get a block device's average seek time, for a given number of seeks.
 *
 * Does a random access read test on a block device, to find its average seek
 * time. Receives the file descriptor of the block device, a pointer to a
 * struct with information about the device, the number of seeks to be
 * performed, and the amount of time it takes to read a single block of the
 * device, in nanoseconds.
 *
 * p_total_ns, if non-NULL, should point to a uint64_t which will be set to
 * the amount of time that was spent seeking and reading block data, in
 * nanoseconds.
 *
 * Returns the average seek time of the block device, in nanoseconds. Exits in
 * case of error.  Requires randomness to be previously initialized (call
 * init_randomness).
 */
static uint64_t get_seek_for_count(int fd, const struct blkdev_info *blkdev_info,
        unsigned int num_seeks, uint64_t block_read_ns, uint64_t *p_total_ns)
{
    const unsigned int block_size = blkdev_info->block_size;
    const uint64_t num_blocks = blkdev_info->num_blocks;
    const uint64_t randaccess_reading_ns = block_read_ns * num_seeks;
    struct timespec start, end;
    uint64_t delta_ns;
    char *buffer;
    unsigned int i;

    assert(num_seeks > 0);

    buffer = allocate_aligned_memory(blkdev_info->alignment, block_size);

    printf("Performing %u random reads, please wait a few seconds...\n",
           num_seeks);

    get_cur_timestamp(&start);
    for (i=0; i<num_seeks; i++)
    {
        /* TODO: We shouldn't include the time it takes to generate a
         * random number in the measurements. Take granular measurements
         * around the read_at, and accumulate the values. */
        uint64_t block_idx = random64() % num_blocks;

        read_at(fd, buffer, block_size, block_idx * block_size);
    }
    get_cur_timestamp(&end);

    free(buffer);

    delta_ns = timespec_diff_ns(&end, &start);

    if (p_total_ns != NULL)
        *p_total_ns = delta_ns;

    /* randaccess_reading_ns is a calculated estimate; protect against
     * integer underflow if actual time measured (including seek time) was
     * lower than that */
    return (delta_ns > randaccess_reading_ns)
           ? (delta_ns - randaccess_reading_ns) / num_seeks
           : 0;
}



/*
 * Get a block device's average seek time.
 *
 * Does random access read tests on a block device, to find its average seek
 * time. Receives the file descriptor of the block device, a pointer to a
 * struct with information about the device, the (optional) number of seeks
 * to be performed, and the amount of time it takes to read a single block
 * of the device, in nanoseconds.
 *
 * The random access tests are done by calling get_seek_for_count.
 *
 * p_total_ns, if non-NULL, should point to a uint64_t which will be set to
 * the amount of time that was spent seeking and reading block data, in
 * nanoseconds. p_randaccess_reading_ns, if non-NULL, should point to a
 * uint64_t which will be set to the estimated amount of time that was
 * spent actually reading the data during the seek test (i.e. minus the
 * seeks themselves).
 *
 * Returns the average seek time of the block device, in nanoseconds. Exits in
 * case of error.  Requires randomness to be previously initialized (call
 * init_randomness).
 */
static uint64_t get_seek_ns(int fd, const struct blkdev_info *blkdev_info,
        unsigned int num_seeks, uint64_t block_read_ns, uint64_t *p_total_ns,
        uint64_t *p_randaccess_reading_ns)
{
    uint64_t seek_ns;
    uint64_t randaccess_reading_ns;

    if (num_seeks == 0)
    {   /* autodetect seek count */
        unsigned int total_seeks = 0;
        uint64_t total_ns = 0;

        /* loop increasing seek count until we take at least a certain
         * amount of time doing the seeks; keep track of total time and
         * seeks performed */
        for (num_seeks = DEFAULT_RAND_READ_SEEKS;
             total_ns < MIN_AUTO_RAND_READ_NS && num_seeks <= MAX_AUTO_RAND_READ_SEEKS;
             num_seeks *= 2)
        {
            uint64_t _total_ns;

            /* ignore the return value, we calculate it later */
            (void)get_seek_for_count(fd, blkdev_info, num_seeks,
                    block_read_ns, &_total_ns);

            total_seeks += num_seeks;
            total_ns += _total_ns;
        }

        /* estimate the time it takes to read the blocks of data we seeked
         * to read, based on the average time it takes to read one block */
        randaccess_reading_ns = block_read_ns * total_seeks;

        /* protect against integer overflow, as randaccess_reading_ns is an
         * estimate and may overshoot our measured time, even with seeks*/
        seek_ns = (total_ns > randaccess_reading_ns)
                  ? (total_ns - randaccess_reading_ns) / total_seeks : 0;

        if (p_total_ns != NULL)
            *p_total_ns = total_ns;
    }
    else
    {   /* use specified seek count */
        seek_ns = get_seek_for_count(fd, blkdev_info, num_seeks, block_read_ns,
                p_total_ns);

        randaccess_reading_ns = block_read_ns * num_seeks;
    }

    if (p_randaccess_reading_ns != NULL)
        *p_randaccess_reading_ns = randaccess_reading_ns;

    return seek_ns;
}



/*
 * Initialize random number generator engine.
 */
static inline void init_randomness(void)
{
    srandom(time(NULL));
}



/*
 * Get information about a block device.
 *
 * Receives the file descriptor of the block device, and a pointer to a struct
 * blkdev_info where the results will be stored.
 *
 * Exits in case of error.
 */
static void get_blkdev_info(int fd, struct blkdev_info *blkdev_info)
{
    blkdev_info->block_size = get_physical_block_size(fd);
    blkdev_info->dev_size = get_dev_size(fd);
    blkdev_info->num_blocks = blkdev_info->dev_size / blkdev_info->block_size;

    if (blkdev_info->dev_size < blkdev_info->block_size)
    {
        fprintf(stderr,
                "error: block size (%u) is greater than device itself (%" PRIu64 ")\n",
                blkdev_info->block_size,
                blkdev_info->dev_size);
        exit(1);
    }

    blkdev_info->alignment = get_readbuf_align(fd);
}



/*
 * Run benchmarks on a block device and get results.
 *
 * Receives an open file descriptor of the block device to be tested, and a
 * pointer to a struct benchmark_results where the results will be stored.
 */
static void run_benchmarks(int fd, unsigned int num_seeks, size_t read_size,
        struct benchmark_results *res)
{
    get_blkdev_info(fd, &res->dev_info);

    res->block_read_ns = get_block_read_ns(fd, &res->dev_info, read_size,
            &res->seq_read_bytes, &res->seq_read_ns);

    init_randomness();
    res->seek_ns = get_seek_ns(fd, &res->dev_info, num_seeks,
            res->block_read_ns, &res->total_randaccess_ns,
            &res->randaccess_reading_ns);
    res->num_seeks = num_seeks;
}



/*
 * Print benchmark results.
 *
 * Receives the path of the tested block device, and a struct benchmark_results
 * containing the results to be printed.
 */
static void print_benchmarks(const char *path, const struct benchmark_results *res)
{
    /* device size, in human terms */
    const struct human_value dev_size = humanize_binary_size(res->dev_info.dev_size);

    /* total amount of data read sequentially, in human terms */
    const struct human_value seq_read_total = humanize_binary_size(res->seq_read_bytes);

    /* sequential read time in human terms */
    char *const seq_read_time = humanize_time(res->seq_read_ns, 3);

    /* time it takes to read 1 physical block, in human terms */
    char *const block_read_time = humanize_time(res->block_read_ns, 3);

    /* sequential read speed in human terms */
    /* FIXME: What if we measured 0 ns? Division by zero */
    const struct human_value seq_read_speed = humanize_binary_speed((long double)res->seq_read_bytes
            / ((long double)res->seq_read_ns / 1000000000ULL));

    /* total time spent actually reading data, while doing random access reads */
    char *const randaccess_reading_time = humanize_time(res->randaccess_reading_ns, 3);

    /* total time spent in the random access test, in human terms */
    char *const total_randaccess_time = humanize_time(res->total_randaccess_ns, 3);

    /* total time spent seeking in seconds, while doing random access reads */
    const uint64_t randaccess_seeking_ns =
        res->total_randaccess_ns > res->randaccess_reading_ns
        ? (res->total_randaccess_ns - res->randaccess_reading_ns)
        : 0;
    char *const randaccess_seeking_time = humanize_time(randaccess_seeking_ns, 3);

    /* average seek time, in human terms */
    char *const seek_time = humanize_time(res->seek_ns, 3);

    /* 1 / (seek_ns / 1000000000L) == 1000000000L / seek_ns*/
    const long double seeks_per_second = 1000000000L / (long double)res->seek_ns;

    /* time measurement tolerance, in human terms */
    char *const timing_tolerance = humanize_time(get_timing_tolerance_ns(), 3);

    printf("\n"
           "%s:\n"
           " Physical block size: %u bytes\n"
           " Device size: %.2Lf %s (%" PRIu64 " blocks, %" PRIu64 " bytes)\n"
           "\n"
           " Sequential read speed: %.2Lf %s (%.2Lf %s in %s)\n"
           " Average time to read 1 physical block: %s\n"
           " Total time spent doing random reads: %s\n"
           "   estimated time spent actually reading data inside the blocks: %s\n"
           "   estimated time seeking: %s\n"
           " Random access time: %s\n"
           " Seeks/second: %.3Lf\n"
           "\n"
           " Minimum individual time measurement error: +/- %s\n",
           path,
           res->dev_info.block_size,
           dev_size.value, dev_size.unit,
           res->dev_info.num_blocks,
           res->dev_info.dev_size,
           seq_read_speed.value, seq_read_speed.unit,
           seq_read_total.value, seq_read_total.unit,
           seq_read_time,
           block_read_time,
           total_randaccess_time,
           randaccess_reading_time,
           randaccess_seeking_time,
           seek_time,
           seeks_per_second,
           timing_tolerance);

    free(seq_read_time);
    free(block_read_time);
    free(randaccess_reading_time);
    free(total_randaccess_time);
    free(randaccess_seeking_time);
    free(seek_time);
    free(timing_tolerance);
}


void run_and_print_benchmarks(const char *devname, unsigned int num_seeks,
        size_t read_size)
{
    struct benchmark_results results;
    int fd;

    fd = open(devname, O_RDONLY | O_DIRECT | O_SYNC);
    die_if(fd < 0, "open");

    run_benchmarks(fd, num_seeks, read_size, &results);

    close(fd);

    print_benchmarks(devname, &results);
}

/* vim: set expandtab smarttab shiftwidth=4 softtabstop=4 tw=75 : */
