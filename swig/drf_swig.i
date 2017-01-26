/* -*- c++ -*- */

#define DRF_API

%include "gnuradio.i"			// the common stuff

//load generated python docstrings
%include "drf_swig_doc.i"

%{
#include "drf/digital_rf_sink.h"
#include "drf/dddc.h"
%}


%include "drf/digital_rf_sink.h"
GR_SWIG_BLOCK_MAGIC2(drf, digital_rf_sink);
%include "drf/dddc.h"
GR_SWIG_BLOCK_MAGIC2(drf, dddc);
