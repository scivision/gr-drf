Digital RF for GNU Radio (gr_drf)
=================================

GNU radio blocks for working with Digital RF HDF5 data. See the digital_rf project for details on reading and writing in this format.


Authors
=======

* Ryan Volz (rvolz@mit.edu)
* Juha Vierinan (jvi019@uit.no)


Dependencies
============

Build:

* digital_rf >= 2.3.0
* gnuradio (gnuradio-dev)
* hdf5 (libhdf5-dev)
* python == 2.7 (python-dev)
* numpy (python-numpy)
* boost (libboost-dev)
* swig (swig)
* cppunit (libcppunit-dev)
* pkg-config (pkg-config)
* cmake (cmake)

Runtime:

* digital_rf >= 2.3.0
* gnuradio
* hdf5
* python == 2.7
* numpy
* boost

For thor3.py script:

* pytz
* dateutil


Installation
============

Get the software:

* git clone https://github.com/ryanvolz/gr-drf.git
* cd gr-drf

Build and install:

* mkdir build
* cd build
* cmake ../
* make
* sudo make install

