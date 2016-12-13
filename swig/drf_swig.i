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

%typemap(in) long double {
    npy_longdouble val;
    PyArray_Descr* longdoubleDescr = PyArray_DescrNewFromType(NPY_LONGDOUBLE);

    if (PyArray_CheckScalar($input)) {
        PyArray_CastScalarToCtype($input, &val, longdoubleDescr);
        $1 = (long double) val;
    } else if (PyFloat_Check($input)) {
        $1 = (long double) PyFloat_AsDouble($input);
    } else if (PyInt_Check($input)) {
        $1 = (long double) PyInt_AsLong($input);
    } else if (PyLong_Check($input)) {
        $1 = (long double) PyLong_AsLong($input);
    } else {
        SWIG_exception(SWIG_TypeError, "expected scalar for conversion to long double");
    }
}


%include "drf/digital_rf_sink.h"
GR_SWIG_BLOCK_MAGIC2(drf, digital_rf_sink);

