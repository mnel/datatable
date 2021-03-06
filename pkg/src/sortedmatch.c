#include <R.h>
#define USE_RINTERNALS
#include <Rinternals.h>
#include <Rdefines.h>
//#include <sys/mman.h>
#include <fcntl.h>

#define ENCODING(x) (LEVELS(x) & 76) // get encoding
// LATIN1_MASK (1<<2) | UTF8_MASK (1<<3) | ASCII_MASK (1<<6)

/* for isSortedList */
#define FLIP_SIGN_BIT   0x8000000000000000
#define RESET_NA_NAN    0x0000ffffffffffff
#define SET_NA_NAN_COMP 0xfff8000000000000 // we can directly set complement in hack_na_nan

// change NA/NaN so that their values are consistent across 32/64 bit
// see fastradixdouble for the issues with NA/NaN for why we do it this way
// for comparison - flip'em by sign bit - then compare in unsigned form...
unsigned long long flip_cmp_double(unsigned long long f, Rboolean isnan) {
    if (isnan) {                  // TO DO: ATM, for both i and i-1 we make checks here. if done by ref. we could try to eliminate check at i-1.
        f &= RESET_NA_NAN;
        f |= SET_NA_NAN_COMP;
    }
    unsigned long long mask = -(long long)(f >> 63) | FLIP_SIGN_BIT;
    return f ^ mask;
}

/* for binary search */
// following Kevin's suggestion
// To avoid 'dereferencing type-punned pointer will break strict-aliasing rules' 
// in recent versions of gcc, casting first and then returning
static unsigned long long R_NA_unsigned_long() {
    unsigned long long *ans = (unsigned long long*)(&NA_REAL);
    return (*ans);
}

static unsigned long long R_NaN_unsigned_long() {
    unsigned long long *ans = (unsigned long long*)(&R_NaN);
    return (*ans);
}

int StrCmp(SEXP x, SEXP y);   // in countingcharacter.c

/*
Implements binary search (a.k.a. divide and conquer).
http://en.wikipedia.org/wiki/Binary_search
http://www.tbray.org/ongoing/When/200x/2003/03/22/Binary
http://googleresearch.blogspot.com/2006/06/extra-extra-read-all-about-it-nearly.html.
Potential integer overflow in (low+upp)/2 is avoided using (low+(upp-low)/2).
Differences over standard binary search (e.g. bsearch in stdlib.h) :
  o list of vectors (key of many columns) of different types
  o ties (groups)
  o options to join to prevailing value (roll join a.k.a locf)
*/

SEXP binarysearch(SEXP left, SEXP right, SEXP leftcols, SEXP rightcols, SEXP isorted, SEXP rollarg, SEXP rollends, SEXP nomatch, SEXP tolerance, SEXP retFirst, SEXP retLength, SEXP allLen1)
{
    // If the left table is large and the right table is large, then sorting the left table first may be
    // quicker depending on how long to sort the left table. This is up to user via use of J() or SJ()
    
    R_len_t lr,nr,low,mid,upp,coln,col,lci,rci,len;
    R_len_t prevlow, prevupp, type, newlow, newupp, /*size,*/ lt, rt;
    double tol = REAL(tolerance)[0], roll, rollabs;
    Rboolean nearest=FALSE, enc_warn=TRUE;
    SEXP lc=NULL, rc=NULL;
    unsigned long long *lc_ul, *rc_ul;  // for NA/NaN numerics
    
    // get value of NA and NaN in unsigned long to check for TYPE=REAL case below.
    const unsigned long long R_UNSIGNED_LONG_NA_REAL  = R_NA_unsigned_long();
    const unsigned long long R_UNSIGNED_LONG_NAN_REAL = R_NaN_unsigned_long();

    if (isString(rollarg)) {
        if (strcmp(CHAR(STRING_ELT(rollarg,0)),"nearest") != 0) error("roll is character but not 'nearest'");
        roll=1.0; nearest=TRUE;       // the 1.0 here is just any non-0.0 
    } else {
        if (!isReal(rollarg)) error("Internal error: roll is not character or double");
        roll = REAL(rollarg)[0];   // more common case (rolling forwards or backwards) or no roll when 0.0
    }
    rollabs = fabs(roll);
    if (!isLogical(rollends) || LENGTH(rollends)!=2) error("rollends not a length 2 logical");
    union {
        int i;
        double d;
        SEXP s;
    } lval, rval;
    if (NA_INTEGER != INT_MIN) error("Internal error: NA_INTEGER (%d) != INT_MIN (%d).", NA_INTEGER, INT_MIN);
    if (NA_INTEGER != NA_LOGICAL) error("Have assumed NA_INTEGER == NA_LOGICAL (currently R_NaInt). If R changes this in future (seems unlikely), an extra case is required; a simple change.");
    nr = LENGTH(VECTOR_ELT(right,0));   // num rows in right hand table
    coln = LENGTH(leftcols);    // there may be more sorted columns in x than involved in the join
    for(col=0; col<coln; col++) {
        lci = INTEGER(leftcols)[col];
        if (lci==NA_INTEGER) error("Internal error. Missing column name(s) in sorted attribute of i");
        rci = INTEGER(rightcols)[col];
        if (rci==NA_INTEGER) error("Internal error. Missing column name(s) in sorted attribute of x");
        lt = TYPEOF(VECTOR_ELT(left, lci));
        rt = TYPEOF(VECTOR_ELT(right, rci));
        if (lt != rt) error("typeof x.%s (%s) != typeof i.%s (%s)", CHAR(STRING_ELT(getAttrib(right,R_NamesSymbol),rci)), type2char(rt), CHAR(STRING_ELT(getAttrib(left,R_NamesSymbol),lci)), type2char(lt));
    }
    if (nearest && TYPEOF(VECTOR_ELT(left, INTEGER(leftcols)[coln-1]))==STRSXP) error("roll='nearest' can't be applied to a character column");
    low=-1;
    for (lr=0; lr < LENGTH(VECTOR_ELT(left,0)); lr++) {  // left row (i.e. from i). TO DO: change left/right to i/x
        upp = prevupp = nr;
        low = prevlow = (LOGICAL(isorted)[0]) ? low : -1;
        INTEGER(retFirst)[lr] = INTEGER(nomatch)[0];   // default to no match for NA goto below
        // INTEGER(retLength)[lr] = 0;   // could do this to save the branch and later branches in R to set .N to 0
        INTEGER(retLength)[lr] = INTEGER(nomatch)[0]==0 ? 0 : 1;
        for(col=0; col<coln && low<upp-1; col++) {
            lc = VECTOR_ELT(left,INTEGER(leftcols)[col]);
            rc = VECTOR_ELT(right,INTEGER(rightcols)[col]);

            prevlow = low;
            prevupp = upp;
            type = TYPEOF(lc);
            switch (type) {
            case LGLSXP : case INTSXP :   // including factors
                lval.i = INTEGER(lc)[lr];
                // if (lval.i==NA_INTEGER) goto nextlr; // TO DO: remove 'if' if NA are allowed in key (could do)
                //                                      // break breaks out of this switch, but we want to break this loop
                while(low < upp-1) {
                    mid = low+((upp-low)/2);
                    rval.i = INTEGER(rc)[mid];
                    if (rval.i<lval.i) {          // relies on NA_INTEGER == INT_MIN, tested above with error if not
                        low=mid;
                    } else if (rval.i>lval.i) {   // TO DO:  more efficient 3-way branch using C ?.
                        upp=mid;
                    } else { // rval.i == lval.i)
                        // branch mid to find start and end of this == series [multi-column binary search]
                        // TO DO: not if mult=first or last 
                        newlow = mid;
                        newupp = mid;
                        while(newlow<upp-1) {  // considered switching off these two whiles if only first or last are required. But these loops will be within the final page and all in cache so unlikely to make much difference. And, we need them anyway for penultimate columns (the first for column 2 is unlikely to be the first matching column 1).
                            mid = newlow+((upp-newlow)/2);
                            rval.i = INTEGER(rc)[mid];
                            if (rval.i == lval.i) newlow=mid; else upp=mid;
                            // TO DO: replace if with ?: using essentially c(newlow,upp)[rval==lval]=mid; maybe sign()
                        }
                        while(low<newupp-1) {
                            mid = low+((newupp-low)/2);
                            rval.i = INTEGER(rc)[mid];
                            if (rval.i == lval.i) newupp=mid; else low=mid;
                        }
                        break;
                        // low and upp now surround the group and we only need this range of the next column
                    }
                }
                break;
            case STRSXP :
                // lval_c and rval_c are added to address bugs reg. character encoding - bugs #5159 and #5266
                lval.s = STRING_ELT(lc,lr);
                // if (lval.s==NA_STRING) goto nextlr;
                while(low < upp-1) {
                    mid = low+((upp-low)/2);
                    rval.s = STRING_ELT(rc,mid);
                    // if (rval.s == NA_STRING) error("NA not allowed in keys"); should be in setkey as some NA may be missed by the binary search here.
                    if (lval.s == rval.s) {
                        newlow = mid;
                        newupp = mid;
                        while(newlow<upp-1) {
                            mid = newlow+((upp-newlow)/2);
                            rval.s = STRING_ELT(rc,mid);
                            if (lval.s == rval.s) newlow=mid; else upp=mid;
                        }
                        while(low<newupp-1) {
                            mid = low+((newupp-low)/2);
                            rval.s = STRING_ELT(rc,mid);
                            if (lval.s == rval.s) newupp=mid; else low=mid;
                        }
                        break;
                    } else if (StrCmp(rval.s, lval.s) < 0) {
                    // TO DO: Reinvestigate non-ASCII. Switch can be a column level check that all is ascii
                    // (setkey can check and mark). Used to use Rf_Scollate but was removed from r-devel API.
                    // We're using the last line of scmp in sort.c since we already dealt with NA and == above
                        if (enc_warn && ENCODING(lval.s) != ENCODING(rval.s) && lval.s != NA_STRING && rval.s != NA_STRING) {
                            warning("Encoding of character column '%s' in X is different from column '%s' in Y in join X[Y]. Joins are not implemented yet for non-identical character encodings and therefore likely to contain unexpected results for those entries. Please ensure that character columns have identical encodings for joins.", CHAR(STRING_ELT(getAttrib(right,R_NamesSymbol),col)), CHAR(STRING_ELT(getAttrib(left,R_NamesSymbol),col)));
                            enc_warn=FALSE; // just warn once
                        }
                        low=mid;
                    } else {
                        if (enc_warn && ENCODING(lval.s) != ENCODING(rval.s) && lval.s != NA_STRING && rval.s != NA_STRING) {
                            warning("Encoding of character column '%s' in X is different from column '%s' in Y in join X[Y]. Joins are not implemented yet for non-identical character encodings and therefore likely to contain unexpected results for those entries. Please ensure that character columns have identical encodings for joins.", CHAR(STRING_ELT(getAttrib(right,R_NamesSymbol),col)), CHAR(STRING_ELT(getAttrib(left,R_NamesSymbol),col)));
                            enc_warn=FALSE; // just warn once
                        }
                        upp=mid;
                    }
                }
                break;
            case REALSXP :
                // hack to pull out the binary search results for NA and NaN
                lc_ul = (unsigned long long*)REAL(lc);
                rc_ul = (unsigned long long*)REAL(rc);

                // same comments for INTSXP apply here
                lval.d = REAL(lc)[lr];
                unsigned long long lval_ud, rval_ud;
                lval_ud = lc_ul[lr];
                // if (ISNAN(lval.d)) goto nextlr;
                while(low < upp-1) {
                    mid = low+((upp-low)/2);
                    rval.d = REAL(rc)[mid];
                    rval_ud = rc_ul[mid];
                     // if lval is NA and rval is NaN, upp=mid *must* execute - not sure how to incorporate this into existing if-statements
                    if (rval_ud == R_UNSIGNED_LONG_NAN_REAL && lval_ud == R_UNSIGNED_LONG_NA_REAL) {
                        upp = mid;
                    } else if (rval.d<lval.d-tol || (ISNAN(rval.d) && rval_ud != lval_ud)) {
                        low=mid;
                    } else if (rval.d>lval.d+tol || (ISNAN(lval.d) && lval_ud != rval_ud)) {
                        upp=mid;
                    } else { // rval.d == lval.d) 
                        newlow = mid;
                        newupp = mid;
                        while(newlow<upp-1) {
                            mid = newlow+((upp-newlow)/2);
                            rval.d = REAL(rc)[mid];
                            rval_ud = rc_ul[mid];
                            if (fabs(rval.d-lval.d)<tol || lval_ud == rval_ud) newlow=mid; else upp=mid;
                        }
                        while(low<newupp-1) {
                            mid = low+((newupp-low)/2);
                            rval.d = REAL(rc)[mid];
                            rval_ud = rc_ul[mid];
                            if (fabs(rval.d-lval.d)<tol || lval_ud == rval_ud) newupp=mid; else low=mid;
                        }
                        break;
                    }
                }
                break;
            default:
                error("Type '%s' not supported as key column", type2char(type));
            }
        }
        if (low<upp-1) {                   // if value found low and upp surround it, unlike standard binary search where low falls on it
            INTEGER(retFirst)[lr] = low+2; // extra +1 for 1-based indexing at R level
            len = upp-low-1;
            INTEGER(retLength)[lr] = len;
            if (len > 1) LOGICAL(allLen1)[0] = FALSE;
        } else if (roll!=0.0 && col==coln && lc && rc && (low>prevlow || upp<prevupp)) {
            // '&& lc && rc' is for test 133 (empty x).  Testing double roll!=0.0 is ok here, i.e. !(roll==FALSE). 
            // runs once per i row (not each search test), so not hugely time critical
            if (low != upp-1 || low<prevlow || upp>prevupp) error("Internal error: low!=upp-1 || low<prevlow || upp>prevupp");
            if (nearest) {   // value of roll ignored currently when nearest
                if ( low>prevlow && upp<prevupp ) {
                    if (  ( TYPEOF(lc)==REALSXP && REAL(lc)[lr]-REAL(rc)[low] <= REAL(rc)[upp]-REAL(lc)[lr] )
                       || ( TYPEOF(lc)<=INTSXP && INTEGER(lc)[lr]-INTEGER(rc)[low] <= INTEGER(rc)[upp]-INTEGER(lc)[lr] )) {
                        INTEGER(retFirst)[lr] = low+1;
                        INTEGER(retLength)[lr] = 1;
                    } else {
                        INTEGER(retFirst)[lr] = upp+1;
                        INTEGER(retLength)[lr] = 1;
                    }
                } else if (upp==prevupp && LOGICAL(rollends)[1]) {
                    INTEGER(retFirst)[lr] = low+1;
                    INTEGER(retLength)[lr] = 1;
                } else if (low==prevlow && LOGICAL(rollends)[0]) {
                    INTEGER(retFirst)[lr] = upp+1;
                    INTEGER(retLength)[lr] = 1;
                }
            } else {
                if ( (   (roll>0.0 && low>prevlow && (upp<prevupp || LOGICAL(rollends)[1]))
                      || (roll<0.0 && upp==prevupp && LOGICAL(rollends)[1]) )
                  && (   (TYPEOF(lc)==REALSXP && REAL(lc)[lr]-REAL(rc)[low]-rollabs<tol)
                      || (TYPEOF(lc)<=INTSXP && (double)(INTEGER(lc)[lr]-INTEGER(rc)[low])-rollabs<tol ) 
                      || (TYPEOF(lc)==STRSXP)   )) {
                    INTEGER(retFirst)[lr] = low+1;
                    INTEGER(retLength)[lr] = 1;
                } else if 
                   (  (  (roll<0.0 && upp<prevupp && (low>prevlow || LOGICAL(rollends)[0]))
                      || (roll>0.0 && low==prevlow && LOGICAL(rollends)[0]) )
                  && (   (TYPEOF(lc)==REALSXP && REAL(rc)[upp]-REAL(lc)[lr]-rollabs<tol)
                      || (TYPEOF(lc)<=INTSXP && (double)(INTEGER(rc)[upp]-INTEGER(lc)[lr])-rollabs<tol )
                      || (TYPEOF(lc)==STRSXP)   )) {
                    INTEGER(retFirst)[lr] = upp+1;   // == low+2
                    INTEGER(retLength)[lr] = 1;
                }
            }
            if (low>prevlow) low -= 1; // for tests 148 and 1096
            // no point setting upp+=1 as upp gets set to nr again, currently.
        }
        // nextlr :;
    }
    return(R_NilValue);
}

SEXP isSortedList(SEXP l, SEXP w, SEXP tolerance)
{
    R_len_t i,j,nrow,ncol;
    Rboolean b;
    SEXP v;
    unsigned long long *vu;
    double tol = REAL(tolerance)[0];
    ncol = length(l);
    nrow = length(VECTOR_ELT(l,0));
    if (NA_INTEGER != INT_MIN) error("Internal error: NA_INTEGER (%d) != INT_MIN (%d).", NA_INTEGER, INT_MIN);
    if (NA_INTEGER != NA_LOGICAL) error("Have assumed NA_INTEGER == NA_LOGICAL (currently R_NaInt). If R changes this in future (seems unlikely), an extra case is required; a simple change.");
    for (j=1; j<ncol; j++) if (length(VECTOR_ELT(l,j)) != nrow) error("Column %d is length %d which differs from length of column 1 (%d).", j+1, length(VECTOR_ELT(l,j)), nrow);
    if (!isInteger(w)) error("vector w of columns to test isn't an integer vector");
    for (i=1; i<nrow; i++) {
        b = TRUE;
        j = -1;
        while (b && ++j<LENGTH(w)-1) {
            v=VECTOR_ELT(l,INTEGER(w)[j]-1);
            switch (TYPEOF(v)) {      // TO DO: change switch to *(EQ[type])(v,i,i-1)
            case INTSXP : case LGLSXP :
                b = INTEGER(v)[i]==INTEGER(v)[i-1]; break;      // DONE: NA is INT_MIN (checked above) here, so it just works normally
            case STRSXP :
                b = STRING_ELT(v,i)==STRING_ELT(v,i-1); break;  // NA seems to work here as well
            case REALSXP :
                vu = (unsigned long long*)REAL(v); // allows equality checking very straightforward (incl. NA, NaN, -Inf and Inf)
                b = (vu[i] == vu[i-1] || fabs(REAL(v)[i] - REAL(v)[i-1])<tol); break; // DONE: NA, NaN, -Inf and Inf
            default :
                error("Type '%s' not supported", type2char(TYPEOF(v))); 
            }
        }
        v = VECTOR_ELT(l,INTEGER(w)[j]-1);
        switch (TYPEOF(v)) {
        case INTSXP : case LGLSXP :
            b = INTEGER(v)[i]<INTEGER(v)[i-1]; break;    // DONE: Check assumption NA largest negative
        case STRSXP :
            b = StrCmp(STRING_ELT(v,i), STRING_ELT(v,i-1))<0; break;
        case REALSXP :
            vu = (unsigned long long*)REAL(v);
            b = flip_cmp_double(vu[i], ISNAN(REAL(v)[i])) < flip_cmp_double(vu[i-1], ISNAN(REAL(v)[i-1])); break; // DONE: NA, NaN, -Inf and Inf
        default :
            error("Type '%s' not supported", type2char(TYPEOF(v))); 
        }
        if (b) return(ScalarLogical(FALSE));
    }
    return(ScalarLogical(TRUE));
}

// require(data.table)
// set.seed(1L)
// DT <- setDT(lapply(1:2, function(x) sample(c(-1e4:1e4), 1e8, TRUE)))
// system.time(setkey(DT))

 // no NA/NaN implementation (Matthew's way using REAL(.) < REAL(.)')
// system.time(data.table:::is.sorted(as.list(DT))[1L])
//  user  system elapsed 
// 0.830   0.001   0.850 
// system.time(data.table:::is.sorted(as.list(DT)))
//  user  system elapsed 
// 0.826   0.002   0.828 

// with unsigned long long implementation:
// system.time(data.table:::is.sorted(as.list(DT))[1L])
//  user  system elapsed 
// 1.168   0.002   1.172 
// system.time(data.table:::is.sorted(as.list(DT)))
//  user  system elapsed 
// 1.173   0.002   1.187 

// conclusion: about .33 sec checking NA/NaN/-Inf/Inf! Not bad, I suppose? Can we make it quicker?
// Note that this is on 1e8. On 1e7 size, this doesn't matter at all as the time to check is 0.07 sec.


/*SEXP sortedintegermatch (SEXP ans, SEXP left, SEXP right, SEXP nomatch)
{
    // As sortedstringmatch, see comments above, but for integers.
    int lr,nr,low,mid,upp;
    if (NA_INTEGER > 0) error("expected internal value of NA_INTEGER %d to be negative",NA_INTEGER);
    nr = length(right); 
    for (lr=0; lr < length(left); lr++) {
        low = -1;
        upp = nr;
        while(low < upp-1) {
            mid = low + (upp-low)/2;
            if (INTEGER(left)[lr]>INTEGER(right)[mid]) {
                low=mid;
            } else {
                upp=mid;
            }
        }
        INTEGER(ans)[lr] = (upp<nr && INTEGER(left)[lr] == INTEGER(right)[upp]) ? upp+1 : INTEGER(nomatch)[0];
    }
    return(R_NilValue);
}*/


