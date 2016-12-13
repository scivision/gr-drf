/* -*- c++ -*- */

#define DRF_API

%include "gnuradio.i"			// the common stuff

//load generated python docstrings
%include "drf_swig_doc.i"

%{
#define SWIG_FILE_WITH_INIT
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include "drf/digital_rf_sink.h"
%}

// needed to initialize numpy C api and not have segfaults
%init %{
    import_array();
%}

%typemap(in) long double (npy_longdouble val) %{
    //PyArray_Descr * longdoubleDescr = PyArray_DescrNewFromType(NPY_LONGDOUBLE);

    if (PyArray_IsScalar($input, LongDouble)) {
    //if (PyArray_CheckAnyScalar($input)) {
        PyArray_ScalarAsCtype($input, &val);
        //PyArray_CastScalarToCtype($input, &$1, longdoubleDescr);
        $1 = (long double) val;
    } else {
        SWIG_exception(SWIG_TypeError, "numpy longdouble expected");
    }
%}


%include "drf/digital_rf_sink.h"
GR_SWIG_BLOCK_MAGIC2(drf, digital_rf_sink);

