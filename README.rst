hdtime
======

|License|

Performance measurements for hard drives and other block devices.

This program does read tests on a block device and provides several timing
values for benchmark and comparison purposes. All tests are read-only; any data
on the device is left untouched.

Usage
-----

Using hdtime is quite simple. Here for example we see the timing results for
``/dev/sdb``, which is a 2 TB 7200 rpm mechanical hard drive:

.. code::

    ~ # ./hdtime /dev/sdb
    hdtime 0.1
    Copyright (C) 2012 Israel G. Lugo
    
    Reading 128.00 MiB to determine sequencial read time, please wait...
    Performing 200 random reads, please wait a few seconds...
    
    /dev/sdb:
     Physical block size: 512 bytes
     Device size: 1907729.09 MiB (3907029168 blocks, 2000398934016 bytes)
    
     Sequencial read speed: 92.10 MiB/s (128.00 MiB in 1.389774 s)
     Average time to read 1 physical block: 0.005302 ms
     Total time spent doing random reads: 3.035313 s
       estimated time spent actually reading data inside the blocks: 0.001060 s
       estimated time seeking: 3.034252 s
     Random access time: 15.171 ms
     Seeks/second: 65.914
    
     Minimum time measurement error: +/- 0.000074 ms


hdtime works well with SSDs too. Here we have the timing results for a 256 GB model:

.. code::

    ~ # ./hdtime /dev/sda
    hdtime 0.1
    Copyright (C) 2012 Israel G. Lugo
    
    Reading 128.00 MiB to determine sequencial read time, please wait...
    Performing 200 random reads, please wait a few seconds...
    
    /dev/sda:
     Physical block size: 512 bytes
     Device size: 244198.34 MiB (500118192 blocks, 256060514304 bytes)
    
     Sequencial read speed: 529.72 MiB/s (128.00 MiB in 0.241636 s)
     Average time to read 1 physical block: 0.000922 ms
     Total time spent doing random reads: 0.013430 s
       estimated time spent actually reading data inside the blocks: 0.000184 s
       estimated time seeking: 0.013246 s
     Random access time: 0.066 ms
     Seeks/second: 15099.137
    
     Minimum time measurement error: +/- 0.000028 ms



.. |License| image:: https://img.shields.io/badge/license-GPLv3+-blue.svg?maxAge=2592000
   :target: LICENSE
