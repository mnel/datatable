#include <R.h>
#define USE_RINTERNALS
#include <Rinternals.h>
#include <Rdefines.h>
#include <R_ext/Rdynload.h>
#include <R_ext/Visibility.h>

// .Calls
SEXP setattrib();
SEXP binarysearch();
SEXP assign();
SEXP dogroups();
SEXP copy();
SEXP shallowwrapper();
SEXP alloccolwrapper();
SEXP selfrefokwrapper();
SEXP truelength();
SEXP settruelength();
SEXP setcharvec();
SEXP setcolorder();
SEXP chmatchwrapper();
SEXP countingcharacter();
SEXP duplist();
SEXP readfile();
SEXP reorder();
SEXP rorder_tol();
SEXP rbindlist();
SEXP vecseq();
SEXP copyattr();
SEXP setlistelt();
SEXP setnamed();
SEXP address();
SEXP copyNamedInList();
SEXP fmelt();
SEXP fcast();
SEXP uniqlist();
SEXP uniqlengths();
SEXP fastradixdouble();
SEXP fastradixint();
SEXP isSortedList();
SEXP setrev();
SEXP forder();

// .Externals
SEXP fastmean();

static const
R_CallMethodDef callMethods[] = {
{"Csetattrib", (DL_FUNC) &setattrib, -1},
{"Cbinarysearch", (DL_FUNC) &binarysearch, -1},
{"Cassign", (DL_FUNC) &assign, -1},
{"Cdogroups", (DL_FUNC) &dogroups, -1},
{"Ccopy", (DL_FUNC) &copy, -1},
{"Cshallowwrapper", (DL_FUNC) &shallowwrapper, -1},
{"Calloccolwrapper", (DL_FUNC) &alloccolwrapper, -1},
{"Cselfrefokwrapper", (DL_FUNC) &selfrefokwrapper, -1},
{"Ctruelength", (DL_FUNC) &truelength, -1},
{"Csettruelength", (DL_FUNC) &settruelength, -1},
{"Csetcharvec", (DL_FUNC) &setcharvec, -1},
{"Csetcolorder", (DL_FUNC) &setcolorder, -1},
{"Cchmatchwrapper", (DL_FUNC) &chmatchwrapper, -1},
{"Ccountingcharacter", (DL_FUNC) &countingcharacter, -1},
{"Cduplist", (DL_FUNC) &duplist, -1},
{"Creadfile", (DL_FUNC) &readfile, -1},
{"Creorder", (DL_FUNC) &reorder, -1},
{"Crorder_tol", (DL_FUNC) &rorder_tol, -1},
{"Crbindlist", (DL_FUNC) &rbindlist, -1},
{"Cvecseq", (DL_FUNC) &vecseq, -1},
{"Ccopyattr", (DL_FUNC) &copyattr, -1},
{"Csetlistelt", (DL_FUNC) &setlistelt, -1},
{"Csetnamed", (DL_FUNC) &setnamed, -1},
{"Caddress", (DL_FUNC) &address, -1},
{"CcopyNamedInList", (DL_FUNC) &copyNamedInList, -1},
{"Cfmelt", (DL_FUNC) &fmelt, -1}, 
{"Cfcast", (DL_FUNC) &fcast, -1}, 
{"Cuniqlist", (DL_FUNC) &uniqlist, -1},
{"Cuniqlengths", (DL_FUNC) &uniqlengths, -1},
{"Cfastradixdouble", (DL_FUNC) &fastradixdouble, -1}, 
{"Cfastradixint", (DL_FUNC) &fastradixint, -1},
{"CisSortedList", (DL_FUNC) &isSortedList, -1},
{"Csetrev", (DL_FUNC) &setrev, -1},
{"Cforder", (DL_FUNC) &forder, -1},
{NULL, NULL, 0}
};


static const
R_ExternalMethodDef externalMethods[] = {
{"Cfastmean", (DL_FUNC) &fastmean, -1},
{NULL, NULL, 0}
};

void setSizes();

void attribute_visible R_init_datatable(DllInfo *info)
// relies on pkg/src/Makevars to mv data.table.so to datatable.so
{
    R_registerRoutines(info, NULL, callMethods, NULL, externalMethods);
    R_useDynamicSymbols(info, FALSE);
    setSizes();
    if (NA_INTEGER != INT_MIN) error("data.table relies on NA_INTEGER [%d] == INT_MIN [%d]. If this is no longer true, please email the maintainer.", NA_INTEGER, INT_MIN);
}


