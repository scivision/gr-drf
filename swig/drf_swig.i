/* -*- c++ -*- */

#define DRF_API

%include "gnuradio.i"			// the common stuff

//load generated python docstrings
%include "drf_swig_doc.i"

%{
#include "drf/digital_rf.h"
%}


%include "drf/digital_rf.h"
GR_SWIG_BLOCK_MAGIC2(drf, digital_rf);
