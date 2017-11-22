hdtime Change Log
=================

All releases and notable changes will be described here.

hdtime adheres to `semantic versioning <http://semver.org>`_. In short, this
means the version numbers follow a three-part scheme: *major version*, *minor
version* and *patch number*.

The *major version* is incremented for releases that break compatibility, such
as removing or altering existing functionality. The *minor version* is
incremented for releases that add new visible features, but are still backwards
compatible. The *patch number* is incremented for minor changes such as bug
fixes, that don't change the public interface.


Unreleased__
------------
__ https://github.com/israel-lugo/hdtime/compare/v0.1.0...HEAD

Added
.....

- Automatically detect the size of the read blocks in the sequential read test.
  This is done by default, unless a size is specified. See `issue #5`_.

- New command-line option ``--read-count``. Controls how many random reads to
  do in the seek test. See `issue #1`_.

- New command-line option ``--read-size``. Controls the size of the read blocks
  in the sequential read test. See `issue #1`_.


Fixed
.....

- Align read buffers properly for direct I/O. Could have caused problems in
  older or non-Linux systems.


0.1.0_ — 2012-07-21
-------------------

First working release.

.. _issue #5: https://github.com/israel-lugo/hdtime/issues/5
.. _issue #1: https://github.com/israel-lugo/hdtime/issues/1

.. _0.1.0: https://github.com/israel-lugo/hdtime/tree/v0.1.0
