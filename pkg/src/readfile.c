#include <R.h>
#define USE_RINTERNALS
#include <Rinternals.h>
#include <Rdefines.h>
#include <ctype.h>
#include <errno.h>

#ifdef WIN32         // means WIN64, too
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>   // for open()
#include <unistd.h>  // for close()
#endif


/*****
TO DO:
893.5 should be two empty integer cols not character
Stroll speedup
* Save continual ch<eof checking throughout. Last line might still be tricky if last line has no eol.
* Whitespace at the end of the line before eol should be ignored, and warn about non white unprotected by comment char
* Warning about any blank lines skipped in the middle.
Check and correct nline in error messages
Go through fread tests and remove all all.equal()s 
Allow logical columns (currently read as character). T/True/TRUE/true are allowed in main/src/util.c
---
Add a way to pick out particular columns only, by name or position.
A way for user to override type, for particular columns only.
Detect and coerce dates and times. By searching for - and :, and dateTtime etc, or R's own method.
CoerceVector should only coerce items read so far.
Read middle and end to check types
A few TO DO inline in the code, including some speed fine tuning
test using at least "grep read.table ...Rtrunk/tests/
Add a mapChunk argument, by default 10% of getsysinfo.ram. Entire file when 0 (if users knowns file will be reread a few times). 
Secondary separator for list() columns, such as columns 11 and 12 in BED.
Allow 2 character separators? Some companies have internal formats that do that.
*****/

char *ch, sep, sep2, eol, eol2, *eof;
int eolLen;
extern double currentTime();

static int countfields()
{
    int ncol=0;
    char *lch;  // lch = local ch
    Rboolean protected;
    lch = ch;
    protected = FALSE;
    if (sep=='\"') error("Internal error: sep is \", not an allowed separator");
    if (lch<eof && *lch!=eol) ncol=1;
    if (lch<eof && *lch=='\"') protected = TRUE;  // lch might be mmp, careful not to check lch-1 for '\' before that (not mapped)
    while (lch<eof && *lch!=eol) {
        if (!protected) ncol += (*lch==sep);
        lch++;
        if (lch<eof && *lch=='\"' && *(lch-1)!='\\') protected = !protected;   // after lch++ it's now safe to check lch-1 for '\' 
    }
    if (protected) error("Unbalanced \" observed on this line: %.*s\n", lch-ch, ch);
    return(ncol);
}

SEXP readfile(SEXP input, SEXP nrowsarg, SEXP headerarg, SEXP nastrings, SEXP verbosearg, SEXP autostart)
{
    SEXP thiscol, ans, thisstr;
    R_len_t i, j, k, protecti=0, nrow=0, ncol=0, nline, flines;
    int thistype;
    char *fnam=NULL, *pos, *pos1, *mmp, *ch2;
    Rboolean verbose=LOGICAL(verbosearg)[0], header=LOGICAL(headerarg)[0], allchar;
    double t0 = currentTime(), tMap;
    size_t filesize;
#ifdef WIN32
    HANDLE hFile=0;
    HANDLE hMap=0;
    DWORD dwFileSize=0;
#else
    int fd=-1;
#endif

    // ********************************************************************************************
    // Point to text input, or open and mmap file
    // ********************************************************************************************
    ch = ch2 = (char *)CHAR(STRING_ELT(input,0));
    while (*ch2!='\n' && *ch2) ch2++;
    if (*ch2=='\n' || !*ch) {
        if (verbose) Rprintf("Input is either empty or contains a \\n, taking this to be text input (not a filename)\n");
        mmp = ch;
        filesize = strlen(mmp);
        eof = mmp+filesize;
        if (*eof!='\0') error("Internal error: last byte of character input isn't \\0");
        tMap = currentTime();
    } else {
        fnam = ch;
#ifndef WIN32
        fd = open(fnam, O_RDONLY);
        if (fd==-1) error("file not found: %s",fnam);
        struct stat stat_buf;
        if (fstat(fd,&stat_buf) == -1) {close(fd); error("Opened file ok but couldn't obtain file size: %s", fnam);}
        filesize = stat_buf.st_size;
        if (filesize==0) {close(fd); error("File is empty: %s", fnam);}
        mmp = (char *)mmap(NULL, filesize, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);    // TO DO?: MAP_HUGETLB
        if (mmp == MAP_FAILED) {
            close(fd);
#else
        // Following: http://msdn.microsoft.com/en-gb/library/windows/desktop/aa366548(v=vs.85).aspx
        hFile=CreateFile(fnam, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hFile==INVALID_HANDLE_VALUE) error("File not found: %s",fnam);
        dwFileSize=GetFileSize(hFile,NULL);
        if (dwFileSize==0) { CloseHandle(hFile); error("File is empty: %s", fnam); }
        filesize = (size_t)dwFileSize;
        hMap=CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, dwFileSize, NULL); // dwFileSize+1 not allowed here, unlike mmap where +1 is zero'd
        if (hMap==NULL) { CloseHandle(hFile); error("This is Windows, CreateFileMapping returned error %d for file %s", GetLastError(), fnam); }
        mmp = (char *)MapViewOfFile(hMap,FILE_MAP_READ,0,0,dwFileSize);
        if (mmp == NULL) {
            CloseHandle(hMap);
            CloseHandle(hFile);
#endif
            if (sizeof(char *)==4)
                error("Opened file ok, obtained its size on disk (%.1fMB), but couldn't memory map it. This is a 32bit machine. You don't need more RAM per se but this fread function is tuned for 64bit addressability, at the expense of large file support on 32bit machines. You probably need more RAM to store the resulting data.table, anyway. And most speed benefits of data.table are on 64bit with large RAM, too. Please either upgrade to 64bit (e.g. a 64bit netbook with 4GB RAM can cost just £300), or make a case for 32bit large file support to datatable-help.", filesize/1024^2);
                // if we support this on 32bit, we may need to use stat64 instead, as R does
            else if (sizeof(char *)==8)
                error("Opened file ok, obtained its size on disk (%.1fMB), but couldn't memory map it. This is a 64bit machine so this is surprising. Please report to datatable-help.", filesize/1024^2);
            else
                error("Opened file ok, obtained its size on disk (%.1fMB), but couldn't memory map it. Size of pointer is %d on this machine. Probably failing because this is neither a 32bit or 64bit machine. Please report to datatable-help.", filesize/1024^2, sizeof(char *));
        }
#ifndef WIN32
        if (madvise(mmp, filesize+1, MADV_SEQUENTIAL | MADV_WILLNEED) == -1) warning("Mapped file ok but madvise failed");
#endif
        tMap = currentTime();
        if (verbose) Rprintf("Memory mapped file ok in %.3f wall clock seconds\n", tMap-t0);
        if (EOF > -1) error("Internal error. EOF is not -1 or less\n");
        if (mmp[filesize-1] < 0) error("mmap'd region has EOF at the end");
        eof = mmp+filesize;  // byte after last byte of file.  Never dereference eof as it's not mapped.
    }
    // ********************************************************************************************
    // Auto detect eol, first eol where there are two (i.e. CRLF)
    // ********************************************************************************************
    ch = mmp;
    while (ch<eof && *ch!='\n' && *ch!='\r') {
        if (*ch=='\"') while(++ch<eof && *ch!='\"');  // allow protection of \n and \r inside column names
        ch++;                                         // this 'if' needed in case opening protection is not closed before eof
    }
    if (ch>=eof) {
        if (ch>eof) error("Internal error: ch>eof when detecting eol");
        if (verbose) Rprintf("Input ends before any \\r or \\n observed. Input will be treated as a single data row.\n");
        eol=eol2='\0'; eolLen=0;
    } else {
        eol=eol2=*ch; eolLen=1;
        if (eol=='\r') {
            if (ch+1<eof && *(ch+1)=='\n') {
                if (verbose) Rprintf("Detected eol as \\r\\n (CRLF) in that order, the Windows standard.\n");
                eol2='\n'; eolLen=2;
            } else if (verbose) Rprintf("Detected eol as \\r only (no \\n afterwards). An old Mac 9 standard, discontinued in 2002 according to Wikipedia.\n");
        } else if (eol=='\n') {
            if (ch+1<eof && *(ch+1)=='\r') {
                warning("Detected eol as \\n\\r, a highly unusual line ending. According to Wikipedia the Acorn BBC used this. If it is intended that the first column on the next row is a character column where the first character of the field value is \\r (why?) then the first column should be quoted (a.k.a protected). Proceeding with attempt to read the file.\n");
                eol2='\r'; eolLen=2;
            } else if (verbose) Rprintf("Detected eol as \\n only (no \\r afterwards), the UNIX and Mac standard.\n");
        } else
            error("Internal error: if no \\r or \\n found then ch should be eof");
    }

    // ********************************************************************************************
    // Auto skip human readable banners, if any
    // ********************************************************************************************
    nline = 0; int lastnonblank = 0; pos = 0;
    ch = mmp;
    while (nline<INTEGER(autostart)[0] && ch<eof) {
        i = 0;
        pos1 = ch;
        while (ch<eof && *ch!=eol) i += !isspace(*ch++);
        nline++;
        if (ch<eof && *ch==eol) ch+=eolLen;
        if (i) pos=pos1, lastnonblank=nline;
    }
    if (lastnonblank==0) error("Input is either empty or fully whitespace in the first 30 rows");
    nline = lastnonblank;   // nline>nonblank when short files (under 30 rows) with trailing newlines
    if (verbose) Rprintf("Starting format detection on line %d (the last non blank line in the first 30)\n", nline);
    ch = pos;
    
    // ********************************************************************************************
    // Auto detect separator and number of fields
    // ********************************************************************************************
    while (ch<eof && isspace(*ch) && *ch!=eol) ch++;             // skip over any leading space at start of row
    if (ch<eof && *ch=='\"') {while(++ch<eof && *ch!='\"' && *ch!=eol); ch++;} // if first column is protected skip over it, and the closing "
    while (ch<eof && (isalnum(*ch) || *ch=='\"' || *ch=='.' || *ch=='+' || *ch=='-')) ch++;
    if (ch==eof) sep=eol; else sep=*ch;
    if (verbose && sep==eol) Rprintf("Line %d ends before any separator was found. Deducing this is a single column input. Otherwise, please specify 'sep' manually, see ?fread.\n", nline);
    ch = pos;                                                    // back to beginning of line
    ncol = countfields();
    if (verbose && sep!=eol) {
        if (sep=='\t') Rprintf("Detected sep as '\\t' and %d columns ('\\t' will be printed as '_' below)\n", ncol);
        else Rprintf("Detected sep as '%c' and %d columns\n", sep, ncol);
    }
    
    // ********************************************************************************************
    // Make informed guess at column types using autostart onwards
    // ********************************************************************************************
    int type[ncol]; for (i=0; i<ncol; i++) type[i]=0;   // default type is 0 (integer)
    union {double d; long long l;} u;
    if (sizeof(u.d) != 8) error("Assumed sizeof(double) is 8 bytes, but it's %d", sizeof(u.d));
    if (sizeof(u.l) != 8) error("Assumed sizeof(long long) is 8 bytes, but it's %d", sizeof(u.l));
    u.l=0; u.d=0;// to avoid 'possibly unitialized' warning from compiler on lines after stroll below
    flines = 0;
    pos1 = ch = pos;   // pos1 used to calc estn.  start of autostart
    while(flines<10 && ch<eof) {
        ch2=ch; while (ch2<eof && isspace(*ch2) && *ch2!=sep && *ch2!=eol) ch2++;  // skip over any empty lines
        if (ch2<eof && *ch2==eol) { ch=ch2+eolLen; continue; }
        flines++;
        for (i=0;i<ncol;i++) {
            //Rprintf("Field %d: '%.20s'\n", i+1, ch);
            while (ch<eof && isblank(*ch) && *ch!=sep) ch++;  // Only needed because strto* skip leading isspace (e.g. " \t123")
            if (ch==eof || *ch==sep || *ch==eol) {}   // skip empty field (",,")
            else if (ch<eof-1 && *ch=='N' && *(ch+1)=='A' && (ch+2==eof || *(ch+2)==sep || *(ch+2)==eol)) ch+=2; // skip ',NA,', doesn't help to detect type
            else switch (type[i]) {
            case 0:  // integer
                errno=0; ch2=ch;
                u.l = strtoll(ch, &ch2, 10);
                if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && INT_MIN<=u.l && u.l<=INT_MAX && errno==0) {
                    ch=ch2;
                    break;
                }
                type[i]++;
            case 1:  // integer64
                errno=0; ch2=ch;
                u.l = strtoll(ch, &ch2, 10);
                if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && errno==0) {
                    ch=ch2;
                    break;
                }
                type[i]++;
            case 2:  // double
                errno=0; ch2=ch;
                u.d = strtod(ch, &ch2);
                if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && errno==0) {
                    ch=ch2;
                    break;
                }
                type[i]++;
            case 3:   // character
                if (ch<eof && *ch=='\"') {while(++ch<eof && *ch!=eol && !(*ch=='\"' && *(ch-1)!='\\')); ch++;}
                else while(ch<eof && *ch!=sep && *ch!=eol) ch++;
            }
            if (ch<eof && *ch==sep && i<ncol-1) {ch++; continue;}  // most common case first, done, next field
            if (i<ncol-1) error("Expected sep ('%c') but '%c' ends field %d on line %d when detecting types: %.*s", sep, *ch, i+1, nline+nrow, ch-pos+1, pos);
        }
        while (ch<eof && *ch!=eol) ch++;
        if (ch<eof && *ch==eol) ch+=eolLen;
    }
    if (verbose) { Rprintf("Type codes: "); for (i=0; i<ncol; i++) Rprintf("%d",type[i]); Rprintf("\n"); }
    int typesxp[4] = {INTSXP,REALSXP,REALSXP,STRSXP};
    
    // ********************************************************************************************
    // Search backwards to find column names row or first data row
    // ********************************************************************************************
    ch = pos;  // back to start of autostart.  nline is already this line number
    if (ch>mmp) {
        if (*(ch-1)!=eol2) error("Internal error. No eol2 immediately before autostart line %d, '%.5s' instead", nline, ch-1);
        do {
            ch-=(1+eolLen);
            while (ch>mmp && *ch!=eol) ch--;
            if (ch>mmp) ch+=eolLen;
            nline--;
        } while ((i=(countfields()==ncol)) && ch>mmp);
        if (!i) {while (*ch++!=eol); ch+=eolLen-1;}
    }
    pos = ch;
    if (verbose) Rprintf("Found first row with %d fields occuring on line %d (either column names or first row of data)\n", ncol, nline);
    
    // ********************************************************************************************
    // Detect and assign column names
    // ********************************************************************************************
    if (header!=NA_LOGICAL) Rprintf("'header' changed by user from 'auto' to %s\n", header?"TRUE":"FALSE");
    SEXP names = PROTECT(allocVector(STRSXP, ncol));
    protecti++;
    i=0; allchar=TRUE;
    for (i=0; i<ncol; i++) {
        if (ch<eof && *ch=='\"') {while(++ch<eof && *ch!=eol && !(*ch=='\"' && *(ch-1)!='\\')); if (ch<eof && *ch++!='\"') error("Format error on line %d: %.*s", nline+flines, ch-pos+1, pos); }
        else {
            errno = 0;
            u.d = strtod(ch, &ch2);
            if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && !errno) { allchar=FALSE, ch=ch2; }
            else while(++ch<eof && *ch!=eol && *ch!=sep); 
        }
        if (i<ncol-1) {   // not the last column (doesn't have a separator after it)
            if (ch<eof && *ch!=sep) error("Unexpected character (%.5s) ending field %d of line %d", ch, i+1, nline);
            else if (ch<eof) ch++;
        }
    }
    // *** TO DO discard any whitespace after last column name on first row before the eol ***
    if (ch<eof && *ch!=eol) error("Not positioned correctly after testing format of header row. ch='%c'",*ch);
    if (!allchar || header==FALSE) {
        if (verbose && !allchar) Rprintf("The first data row has some non character fields. Treating as a data row and using default column names.\n");
        char buff[10];
        for (i=0; i<ncol; i++) {
            sprintf(buff,"V%d",i+1);
            SET_STRING_ELT(names, i, mkChar(buff));
        }
        ch = pos;   // back to start of first row. Treat as first data row, no column names present.
    } else {
        if (verbose && allchar) Rprintf("All the fields in the first data row are character fields. Treating as the column names\n");
        ch = pos;
        nline++;
        for (i=0; i<ncol; i++) {
            if (ch<eof && *ch=='\"') {ch++; ch2=ch; while(ch2<eof && *ch2!='\"' && *ch2!=eol) ch2++;}
            else {ch2=ch; while(ch2<eof && *ch2!=sep && *ch2!=eol) ch2++;}
            SET_STRING_ELT(names, i, mkCharLen(ch, ch2-ch));
            if (ch2<eof && *ch2=='\"') ch2++;
            if (i<ncol-1) ch=++ch2;
        }
        while (ch<eof && *ch!=eol) ch++;
        if (ch<eof && *ch==eol) ch+=eolLen;  // now on first data row (row after column names)
        pos = ch;
    }
    
    // ********************************************************************************************
    // Count number of rows
    // ********************************************************************************************
    if (pos==eof || *pos==eol) {
        nrow=0;
        if (verbose) Rprintf("Byte after header row is eof or eol, 0 data rows present.\n");
    } else {
        nrow=1; while (ch<eof) nrow+=(*ch++==eol);
        if (verbose) Rprintf("Count of eol after pos: %d\n",nrow);
        // Advantages of exact count: i) no need to slightly over allocate (by 5%, say) and no need to clear up on heap during gc(),
        // and ii) no need to implement realloc if estimate doesn't turn out to be large enough (e.g. sample rows wider than file average).
        // The old estimate method :
        // estn = (R_len_t)ceil(1.05 * flines * (filesize-(pos-mmp)) / (pos2-pos1)) +5;  // +5 for small files
        // if (verbose) Rprintf("Estimated nrows: %d ( 1.05*%d*(%ld-(%ld-%ld))/(%ld-%ld) )\n",estn,flines,filesize,pos,mmp,pos2,pos1);
        if (ch!=eof) error("Internal error: ch!=eof after counting eol");
        i=0; Rboolean blank=TRUE;
        while (blank && ch>pos) {   // subtract blank lines at the end from the row count
            while (ch>pos && *--ch!=eol2);
            if (ch>pos) ch++;
            i += (blank=(countfields()!=ncol));
            ch -= eolLen;
            // TO DO Warn about any non blank lines with incorrect number of fields being removed.
        }
        nrow-=i;
        if (verbose) Rprintf("Subtracted %d for last eol, plus any trailing empty lines, leaving %d data rows\n",i,nrow);
    }
    i = INTEGER(nrowsarg)[0];
    if (i>-1 && i<nrow) {
        nrow = i;
        if (verbose) Rprintf("nrow limited to nrows passed in (%d)\n", nrow);
    }
    ch = pos;  

    // ********************************************************************************************
    // Allocate columns for known nrow
    // ********************************************************************************************
    ans=PROTECT(allocVector(VECSXP,ncol));  // safer to leave over allocation to alloc.col on return in fread.R
    protecti++;
    setAttrib(ans,R_NamesSymbol,names);
    for (i=0; i<ncol; i++) {
        thistype  = typesxp[ type[i] ];  // map from 0-3 to INTSXP(0), REALSXP(1), REALSXP(2) and STRSXP(3).
        thiscol = PROTECT(allocVector(thistype,nrow));
        protecti++;
        if (type[i]==1) setAttrib(thiscol, R_ClassSymbol, ScalarString(mkChar("integer64")));
        SET_TRUELENGTH(thiscol, nrow);
        SET_VECTOR_ELT(ans,i,thiscol);
    }
    
    // ********************************************************************************************
    // Now read the data
    // ********************************************************************************************
    double t1=currentTime(), tCoerce0, tCoerce=0.0;
    double nexttime = t0+2;  // start printing % done after a few seconds, if doesn't appear then you know mmap is taking a while
    Rboolean isna;
    ch = pos;   // back to start of first data row
    for (i=0; i<nrow; i++) {
        //Rprintf("Row %d : %.10s\n", i+1, ch);
        while (ch<eof && isspace(*ch) && *ch!=sep && *ch!=eol) ch++;       // skip over any empty lines.
        if (ch<eof && *ch==eol) { ch+=eolLen; nline++; continue; }
        ch = pos;                                 //  back to start in case first column is character with leading space to be retained
        for (j=0;j<ncol;j++) {
            //Rprintf("Field %d: '%.10s'\n", j+1, ch);
            thiscol = VECTOR_ELT(ans, j);
            switch (type[j]) {
            case 0: // integer
                errno=0; ch2=ch;
                while (ch<eof && isspace(*ch) && *ch!=sep && *ch!=eol) ch++; // TO DO: remove. Only because stroll* skips leading isspace (could include sep)
                if (ch==eof || *ch==sep || *ch==eol) ch2=ch;
                else {ch2=ch; u.l = strtoll(ch, &ch2, 10);}
                if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && INT_MIN<=u.l && u.l<=INT_MAX && errno==0) {
                    // TO DO: do we really need the check on ch2?  sep can't be true unless ch2 has moved?
                    INTEGER(thiscol)[i] = (int)u.l;
                    ch=ch2;
                    break;   // field now done, we prefer fully populated files
                }
                isna=FALSE;
                if (ch==eof || *ch==sep || *ch==eol || (isna=(ch<eof-1 && *ch=='N' && *(ch+1)=='A' && (ch+2==eof || *(ch+2)==sep || *(ch+2)==eol)))) {
                    INTEGER(thiscol)[i] = NA_INTEGER;
                    ch+=2*isna;
                    break;
                }  
            case 1: // bit64::integer64
                errno=0; ch2=ch;
                while (ch<eof && isspace(*ch) && *ch!=sep && *ch!=eol) ch++; // TO DO: remove. Only because stroll* skips leading isspace (could include sep)
                if (ch==eof || *ch==sep || *ch==eol) ch2=ch;
                else {ch2=ch; u.l = strtoll(ch, &ch2, 10);}   // wasteful repeated call on fall through, but only once (ever, per column) on bump.
                if (ch2>ch && (ch2==eof || *ch2==sep || *ch2==eol) && errno==0) {
                    if (type[j]==0) { error("Coercing int to integer64 needs to be implemented"); type[i]=1; }
                    REAL(thiscol)[i] = u.d;
                    ch=ch2;
                    break;
                }
                isna=FALSE;
                if (ch==eof || *ch==sep || *ch==eol || (isna=(ch<eof-1 && *ch=='N' && *(ch+1)=='A' && (ch+2==eof || *(ch+2)==sep || *(ch+2)==eol)))) {
                    REAL(thiscol)[i] = NA_REAL;
                    ch+=2*isna;
                    break;
                }
            case 2: // double
                errno=0; ch2=ch;
                while (ch<eof && isspace(*ch) && *ch!=sep && *ch!=eol) ch++; // TO DO: remove. Only because strod* skips leading isspace (could include sep)
                if (ch==eof || *ch==sep || *ch==eol) ch2=ch;
                else {ch2=ch; u.d = strtod(ch, &ch2);}
                if (ch2>ch && (ch==eof || *ch2==sep || *ch2==eol) && errno==0) {
                    if (type[j]==1) { error("Coercing integer64 to real needs to be implemented"); type[j]=2; }
                    else if (type[j]==0) {
                        tCoerce0 = currentTime();
                        SET_VECTOR_ELT(ans, j, thiscol = coerceVector(thiscol, REALSXP));
                        if (verbose) Rprintf("Column %d bumped from code %d to double on line %d, field contains '%.10s'\n", j+1, type[j], i+2, ch);
                        type[j]=2;
                        tCoerce += currentTime()-tCoerce0;
                    }
                    REAL(thiscol)[i] = u.d;
                    ch=ch2;
                    break;
                }
                isna=FALSE;
                if (ch==eof || *ch==sep || *ch==eol || (isna=(ch<eof-1 && *ch=='N' && *(ch+1)=='A' && (ch2==eof || *(ch+2)==sep || *(ch+2)==eol)))) {
                    REAL(thiscol)[i] = NA_REAL;
                    ch+=2*isna;
                    break;
                }
            case 3: // character
                ch2=ch;
                if (ch<eof && *ch=='\"') {ch++; while(++ch2<eof && *ch2!=eol && !(*ch2=='\"' && *(ch2-1)!='\\'));}
                else while (ch2<eof && *ch2!=sep && *ch2!=eol) ch2++;
                if (type[j]<3) {
                    if (type[j]==1) error("Coercing integer64 to character needs to be implemented");
                    tCoerce0 = currentTime();
                    SET_VECTOR_ELT(ans, j, thiscol = coerceVector(thiscol, STRSXP));
                    for (k=0; k<nrow; k++) if (STRING_ELT(thiscol,k)==NA_STRING) SET_STRING_ELT(thiscol,k,R_BlankString);
                    // If a character column was blank on earlier rows (and in the guess rows), they'll have been coerced to
                    // NAs. Set back to "" and these will be converted back to NA at the end if na.strings contains "".
                    if (verbose) Rprintf("Column %d bumped from code %d to character on line %d, field contains '%.*s'\n", j+1, type[j], i+2, ch2-ch, ch);
                    type[j]=3;
                    tCoerce += currentTime()-tCoerce0;
                }
                SET_STRING_ELT(thiscol, i, mkCharLen(ch,ch2-ch));
                if (ch2<eof && *ch2=='\"') ch=ch2+1; else ch=ch2;
            }
            if (ch<eof && *ch==sep && j<ncol-1) {ch++; continue;}  // most common case first, done, next field
            if (j<ncol-1) error("Expected sep ('%c') but '%c' ends field %d on line %d: %.*s", sep, *ch, j+1, i+2, ch-pos+1, pos);
        }
        //Rprintf("At end of line with i=%d and ch='%.10s'\n", i, ch);
        while (ch<eof && *ch!=eol) ch++; // discard after end of line, but before \n. TO DO: warn about uncommented text here
        if (ch<eof && *ch==eol) ch+=eolLen;
        pos = ch;  // start of line position only needed to include the whole line in any error message
        //Rprintf("Start of next line '%.10s'\n", ch);
        //if (++nrow == estn) {
        //    if (INTEGER(nrowsarg)[0]==-1) warning("Filled all estn rows (%d), even after estimating 5%% more. Reallocation at this point fairly trivial but not yet implemented. Returning the rows read so far and discarding the rest (if any), for now.", estn);
        //    break;
        //} else
        if (i%10000==0 && currentTime()>nexttime) {  // %10000 && saves a little bit (apx 0.2 in 4.9 secs, 5%)
            Rprintf("%.0f%%\r", 100.0*i/nrow);         // prints straight away (0%10000) if the mmap took a while
            nexttime = currentTime()+1;
            R_CheckUserInterrupt();
        }
    }
    Rprintf("    \r");
    double t2=currentTime();
    for (i=0; i<ncol; i++) SETLENGTH(VECTOR_ELT(ans,i), nrow);
    for (k=0; k<length(nastrings); k++) {
        thisstr = STRING_ELT(nastrings,k);
        for (j=0; j<ncol; j++) {
            thiscol = VECTOR_ELT(ans,j);
            if (TYPEOF(thiscol)==STRSXP) {
                for (i=0; i<nrow; i++)
                    if (STRING_ELT(thiscol,i)==thisstr) SET_STRING_ELT(thiscol, i, NA_STRING);
            }
        }
    }
    UNPROTECT(protecti);
    if (fnam!=NULL) {
#ifdef WIN32
        UnmapViewOfFile(mmp);
        CloseHandle(hMap);
        CloseHandle(hFile);
#else
        munmap(mmp, filesize);
        close(fd);
#endif
    }
    if (verbose) {
        double tn = currentTime(), tot=tn-t0;
        Rprintf("%8.3fs (%3.0f%%) Memory map (quicker if you rerun)\n", tMap-t0, 100*(tMap-t0)/tot);
        Rprintf("%8.3fs (%3.0f%%) Format detection\n", t1-tMap, 100*(t1-tMap)/tot);
        //Rprintf("%8.3fs (%3.0f%%) Count rows (wc -l)\n", .......);
        //Rprintf("%8.3fs (%3.0f%%) Allocation of %dx%d result (%dMB) in RAM\n", ....., nrow, ncol);
        Rprintf("%8.3fs (%3.0f%%) Reading data\n", t2-t1-tCoerce, 100*(t2-t1-tCoerce)/tot);
        Rprintf("%8.3fs (%3.0f%%) Bumping column type midread and coercing data already read\n", tCoerce, 100*tCoerce/tot);
        Rprintf("%8.3fs (%3.0f%%) Changing na.strings to NA\n", tn-t2, 100*(tn-t2)/tot);
        Rprintf("%8.3fs        Total\n", tot);
    }
    return(ans);
}

