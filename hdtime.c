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

#define NUM_SEEKS 200

#define MIB (1024 * 1024)

/* Amount of bytes to read sequentially in a single block. */
#define SEQ_READ_BYTES (64 * MIB)


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


struct blkdev_info {
    uint64_t dev_size;
    uint64_t num_blocks;
    unsigned int block_size;
    size_t alignment;
};


struct benchmark_results {
    char *path;
    struct blkdev_info dev_info;
    unsigned int seq_read_bytes;
    long double seq_read_time;
    long double block_read_time;
    long double total_randaccess_time;
    long double seek_time;
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

    assert(exp <= sizeof(x) * 8);

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
    if (x > (1U << (sizeof(unsigned int) * 8 - 1)))
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
 * Get a device's physical block size. Receives an open file descriptor for the
 * device. Exits in case of error.
 */
unsigned int get_physical_block_size(int fd)
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
size_t get_readbuf_align(int fd)
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
uint64_t get_dev_size(int fd)
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
void read_at(int fd, void *buffer, size_t count, off64_t offset)
{
    off64_t seek_ok;
    ssize_t read_ok;

    seek_ok = lseek64(fd, offset, SEEK_SET);
    die_if(seek_ok == (off64_t)-1, "lseek64");
    read_ok = read(fd, buffer, count);
    die_if(read_ok < 0, "read");
}



/*
 * Get a timestamp in seconds, with very high precision. The time value is
 * relative to some unspecified starting point; useful for relative time
 * calculations (timing measurements).
 */
long double get_cur_timestamp(void)
{
    struct timespec now;
    int retval;

    retval = clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    die_if(retval == -1, "clock_gettime");

    return (long double)now.tv_sec + (long double)now.tv_nsec / 1000000000.0L;
}



/*
 * Calculate the tolerance in taking two time measurements and calculating the
 * time delta. Returns half the maximum error in seconds; the actual tolerance
 * is +/- the returned value.
 */
long double get_timing_tolerance(void)
{
    struct timespec res;
    int retval;
    long double resolution, t0, t1, delta;

    /* get the underlying clock's resolution (lower bound) */
    retval = clock_getres(CLOCK_MONOTONIC_RAW, &res);
    die_if(retval == -1, "clock_getres");

    resolution = (long double)res.tv_sec
                 + (long double)res.tv_nsec / 1000000000.0L;

    /* measure the actual overhead of measuring time */
    t0 = get_cur_timestamp();
    t1 = get_cur_timestamp();
    delta = t1 - t0;

    /* tolerance is +/- half the maximum error */
    return max(resolution, delta) / 2;
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
 * Get a block device's average block read time.
 *
 * Does a sequencial read test on a block device, to find its average read
 * speed. Receives the file descriptor of the block device and a pointer to a
 * struct with information about the device. p_total_bytes, if non-NULL, should
 * point to an unsigned int which will be set to the total amount of bytes read
 * from the device. p_total_read_time, if non-NULL, should point to a long
 * double which will be set to the amount of time that was spent reading, in
 * seconds.
 *
 * Returns the average time it takes to read a single block of the device.
 * Exits in case of error.
 */
long double get_block_read_time(int fd, const struct blkdev_info *blkdev_info,
        unsigned int *p_total_bytes, long double *p_total_read_time)
{
    size_t read_size = align_ceil(SEQ_READ_BYTES, blkdev_info->alignment);
    char *buffer;
    long double start, end, delta;

    assert(read_size % blkdev_info->alignment == 0);

    if (read_size > blkdev_info->dev_size)
    {
        read_size = blkdev_info->dev_size;
    }

    /* two reads: beginning and end of the device */
    printf("Reading %.2f MiB to determine sequential read time, please wait...\n",
           (double)read_size / MIB * 2);

    buffer = allocate_aligned_memory(blkdev_info->alignment, read_size);

    start = get_cur_timestamp();
    read_at(fd, buffer, read_size, 0);
    read_at(fd, buffer, read_size, blkdev_info->dev_size - read_size);
    end = get_cur_timestamp();

    free(buffer);

    delta = end - start;

    if (p_total_bytes != NULL)
        *p_total_bytes = read_size * 2;

    if (p_total_read_time != NULL)
        *p_total_read_time = delta;

    return delta / ((read_size * 2) / blkdev_info->block_size);
}



/*
 * Get a block device's average seek time.
 *
 * Does a random access read test on a block device, to find its average seek
 * time. Receives the file descriptor of the block device, a pointer to a
 * struct with information about the device, and the amount of time it takes to
 * read a single block of the device, in seconds. p_total_time, if non-NULL,
 * should point to a long double which will be set to the amount of time that
 * was spent seeking and reading block data.
 *
 * Returns the average seek time of the block device. Exits in case of error.
 * Requires randomness to be previously initialized (call init_randomness).
 */
long double get_seek_time(int fd, const struct blkdev_info *blkdev_info,
        long double block_read_time, long double *p_total_time)
{
    const unsigned int block_size = blkdev_info->block_size;
    const uint64_t num_blocks = blkdev_info->num_blocks;
    long double start, end, delta;
    char *buffer;
    int i;

    buffer = allocate_aligned_memory(blkdev_info->alignment, block_size);

    printf("Performing %u random reads, please wait a few seconds...\n",
           NUM_SEEKS);

    start = get_cur_timestamp();
    for (i=0; i<NUM_SEEKS; i++)
    {
        uint64_t block_idx = random64() % num_blocks;

        read_at(fd, buffer, block_size, block_idx * block_size);
    }
    end = get_cur_timestamp();

    free(buffer);

    delta = end - start;

    if (p_total_time != NULL)
        *p_total_time = delta;

    return (delta - block_read_time * NUM_SEEKS) / NUM_SEEKS;
}



/*
 * Initialize random number generator engine.
 */
static void init_randomness(void)
{
    time_t seed;

    time(&seed);
    srandom(seed);
}



/*
 * Get information about a block device.
 *
 * Receives the file descriptor of the block device, and a pointer to a struct
 * blkdev_info where the results will be stored.
 *
 * Exits in case of error.
 */
void get_blkdev_info(int fd, struct blkdev_info *blkdev_info)
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
void benchmark(int fd, struct benchmark_results *res)
{
    get_blkdev_info(fd, &res->dev_info);

    res->block_read_time = get_block_read_time(fd, &res->dev_info, &res->seq_read_bytes, &res->seq_read_time);

    init_randomness();
    res->seek_time = get_seek_time(fd, &res->dev_info, res->block_read_time, &res->total_randaccess_time);
}



/*
 * Print benchmark results.
 *
 * Receives the path of the tested block device, and a struct benchmark_results
 * containing the results to be printed.
 */
void print_benchmarks(const char *path, const struct benchmark_results *res)
{
    /* device size in MiB (divide before converting, to help avoid overflow) */
    const long double dev_size_mib = (long double)(res->dev_info.dev_size / 1024) / 1024;
    /* sequential read speed in MiB/s */
    const long double seq_read_speed = ((long double)res->seq_read_bytes / res->seq_read_time) / MIB;
    /* amount of data read sequentially in MiB */
    const long double seq_read_mib = (long double)res->seq_read_bytes / MIB;
    /* time it takes to read 1 physical block, in ms */
    const long double block_read_ms = res->block_read_time * 1000;
    /* total time spent actually reading data, while doing random access reads */
    const long double randaccess_reading_time = res->block_read_time * NUM_SEEKS;
    /* total time spent seeking, while doing random access reads */
    const long double randaccess_seeking_time = res->total_randaccess_time - randaccess_reading_time;
    /* average seek time in ms */
    const long double seek_time_ms = res->seek_time * 1000;
    const long double seeks_per_second = 1 / res->seek_time;
    /* time measurement tolerance in ms */
    const long double timing_tolerance_ms = get_timing_tolerance() * 1000;

    printf("\n"
           "%s:\n"
           " Physical block size: %u bytes\n"
           " Device size: %.2Lf MiB (%" PRIu64 " blocks, %" PRIu64 " bytes)\n"
           "\n"
           " Sequential read speed: %.2Lf MiB/s (%.2Lf MiB in %.6Lf s)\n"
           " Average time to read 1 physical block: %Lf ms\n"
           " Total time spent doing random reads: %.6Lf s\n"
           "   estimated time spent actually reading data inside the blocks: %.6Lf s\n"
           "   estimated time seeking: %.6Lf s\n"
           " Random access time: %.3Lf ms\n"
           " Seeks/second: %.3Lf\n"
           "\n"
           " Minimum time measurement error: +/- %Lf ms\n",
           path,
           res->dev_info.block_size,
           dev_size_mib,
           res->dev_info.num_blocks,
           res->dev_info.dev_size,
           seq_read_speed,
           seq_read_mib,
           res->seq_read_time,
           block_read_ms,
           res->total_randaccess_time,
           randaccess_reading_time,
           randaccess_seeking_time,
           seek_time_ms,
           seeks_per_second,
           timing_tolerance_ms);
}


int main(int argc, char **argv)
{
    struct benchmark_results results;
    int fd;

    printf("hdtime 0.1\n"
           "Copyright (C) 2012 Israel G. Lugo\n"
           "\n");

    if (argc != 2) {
            printf("Usage: %s <raw disk device>\n", argv[0]);
            exit(1);
    }

    fd = open(argv[1], O_RDONLY | O_DIRECT | O_SYNC);
    die_if(fd < 0, "open");

    benchmark(fd, &results);

    close(fd);

    print_benchmarks(argv[1], &results);

    return 0;
}
