/*
 * WRKDBF, DBU, Filescope etc like utility for OS/400. 
 * Unfinished as I do not have access to such a box anymore.
 * In dire need of refactoring, I was not a clean coder back then. 
 *
 * Copyright (c) 2010 erik olsson
 *
 * This hack is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

/*   Easiest invoked via cmd EOTABKOLL. File must be passed, lib or member is optional.
 *   Compile with, for instance
 *   ixlc /path/to//eotabkoll.c -oMYLIB/EOTABKOLL -qdbgview=source -qprint 
 *  
 */


#pragma comment(copyright,"Erik Olsson, 2010")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
/* OS/400 includes */
#include <qusrobjd.h>
#include <quscrtus.h>
#include <qusdltus.h>
#include <qusptrus.h>
#include <qusec.h>         
#include <qusgen.h>        
#include <quslfld.h>
#include <quslmbr.h>
#include <qdbrtvfd.h>
#include <qusrmbrd.h>
#include <qsqgnddl.h>
#include <qsycusra.h>
#include <qusrjobi.h>
#include <qdbldbr.h>
#include <qsnapi.h>
#include <quscmdln.h>
#include <recio.h>
#include <regex.h>
#include <locale.h>
#include <qtqiconv.h>
/* OS/400 MI-includes */
#include </qsys.lib/qsysinc.lib/mih.file/cpynv.mbr>
#include </qsys.lib/qsysinc.lib/mih.file/matmatr.mbr>
#include </qsys.lib/qsysinc.lib/mih.file/triml.mbr>
#include </qsys.lib/qsysinc.lib/mih.file/cvthc.mbr>

#define true            1
#define false           0
#define OBJSIZE        10
#define LISTSIZE       15
#define LISTWIDTH     131 
#define LISTSTRROW     10
#define LISTSTRCOL      2 
#define RRNCOLSIZE     11
#define MAXFLDROWLEN   50
#define MBRLIST_STRROW  8
#define FILELIB_SIZE   20
#define norm           QSN_SA_NORM
#define blue           QSN_SA_BLU
#define pink           QSN_SA_PNK
#define white          QSN_SA_WHT
#define red            QSN_SA_RED
#define green          QSN_SA_GRN


typedef _Packed struct
{
    char   Field_Name [10];
    char   Data_Type;
    char   Var_Fld_Ind;                /* '1' = varlen */
    int    Output_Buffer_Position;
    int    Field_Length_Bytes;
    int    Digits;
    int    Decimal_Positions;
    int    nbr_DBCS_chars;
    int    data_CCSID;
    char   Column_Heading [40];        /* sums Column_Heading1 o 2 */
    int    dspsize;                    
    int    fldsize;                    
    short  strpos;                     
    char   dataTypeString [15];        
} fielddata_t;


typedef _Packed struct
{
    char   keyname [10];
    unsigned short  Data_Type;
    short  Field_Len;
    short  Num_Of_Digs;
    short  Dec_Pos;
    short  dsplen; /* needs to handel line wrap really */
    /* all we care about for now */
} keystruct;


typedef _Packed struct
{
    short  col;
    char   AID;
    char   field [2000];
} inputData;


typedef struct
{
    Qsn_Win_Desc_T win_desc;
    char           buffer[200];
} storage_t;


typedef struct
{
    Qsn_Ssn_Desc_T sess_desc;
    char           buffer[200];
} ssnstorage_t;


typedef struct
{
    char membername [10];
    char mbrtxt     [50];
} mbrnames;


typedef struct
{
    Qsn_Fld_Inf_T fldinfo;
    char          buffer[200];
} fldinfo_t;


typedef struct
{
    Qdb_Dbrl0100_t dbrinfo;
    char   filetextdesc[52];
    char   keystr[70];
    int    authorized;
    short  nbrkeys;
} dbrinfo_t;


static void loadColumnNames(char **, char **, int);
static void writeColumnNames(char *, char **, int, int, int, int);
static void pageUp(_RFILE *, _RIOFB_T *, int *, int *);
static void pageDown(int *);
static void loadHdr(Qdb_Qdbfh_t *, Qdb_Qdbfphys_t *, Qdb_Qdbflogl_t *, Qdb_Mbrd0200_t *, char *, char **, int, int);
static void loadFooter(void);
static void loadlist(_RFILE *, _RIOFB_T *, Qdb_Qdbfh_t *, int *, int, unsigned long *, int, int, int, char);
static int  handleRightmostView(int, int, int, int);
static void shiftView(int, int, int, unsigned long *);
static void cleanUp(void);
static int padWithSpaces(int, int, int, int);
static void setWindowArea(Qsn_Win_Desc_T *,
                   int toprow,
                   int leftcolumn,
                   int nbrrows,
                   int nbrcolumns);
static void drawJrnWindow(const Qsn_Win_T *,
                   const Qsn_Cmd_Buf_T *);
static int displayKeyWindow(_RFILE *, _RIOFB_T *, size_t);
static void setKeyWindowFields(short *, short *);
static void displayWindow(Qsn_Win_T);
static void loadColDataSession(Qsn_Ssn_T *, int);
static void displaySession(Qsn_Ssn_T ssn);
static int generateDDL(int, char *, Qsn_Ssn_T *, const Qdb_Qdbfh_t *, const Qdb_Qdbflogl_t *);
static void dspSingleRow(int, int, int, char *);
static void setSingleRowFlds(int, short *, short *, int *, int *, char *, int, int, int);
static void pageUpSingleRow(short *, int *, int *, int);
static int createMemberList(char *, char *, mbrnames **);
static int displayMemberWindow(Qsn_Win_T *, mbrnames *, char *, char *, short, int);
static void loadMemberList(Qsn_Win_T, mbrnames *, short, int *, int, int, int *);
static int createDBRList(char *, char *, dbrinfo_t **, int *);
static int displayDBRWindow(Qsn_Win_T *, dbrinfo_t *, char *, char *, int);
static void loadDBRList(Qsn_Win_T, dbrinfo_t *, int, int *, int, int, int *, int);


/* File local variables                         */
char mcattr;
Qsn_Cmd_Buf_T cmdbuf;
Qsn_Inp_Buf_T inputbuf;
char *listData [LISTSIZE];
int listrrn,
    len,
    rowcount = 0,
    wpos = 0;
inputData *pinput;

Qus_Generic_Header_0100_t *space;
char *char_space;
Qdb_Lfld_Header_t   *field_hdr;
Qdb_Lfld_FLDL0100_t *field_list;
fielddata_t *fptr;

Qdb_Qdbfjoal_t   *jrndata;
Qdb_Qdbwh_t      *keydata;
Qdb_Qdbwhkey_t   *keys;
keystruct        *keyfldinfo;

Qsn_Win_T jrnwin,
          keywin,
          sngrowwin;
Qsn_Win_Desc_T win_desc;
Q_Bin4 envhandle,
       win_desc_length = sizeof(win_desc);
Qsn_Win_Ext_Inf_T ext = { NULL, NULL, NULL, NULL, NULL, NULL };


/*
*  main
*/
int main (int argc, char *argv[])
{    
    int fieldno,
        i,
        j = 0,
        nbrAuthorities = 3, /* use (for various APIs) and read */
        maxfldsize = 0,
        nbrColumns = 0,
        displayRRN = false,
        listw = LISTWIDTH,
        strlistcol = LISTSTRCOL,
        listend = false,
        listtop = false,
        dspreclen = 0,
        dspcolnames = true;
    unsigned long posrrn = 0;
    size_t keylen = 0;
    char usname[20] = "EODBFLDS  QTEMP     ";
    char fileflags[15];
    char filename[21];
    char mbrname[OBJSIZE];
    char cmdstring[70];
    char qualfilename[33];
    char decpoint;
    int arglen[3];
    char error_code[201];
    Qus_EC_t *err;
    char *pch,
         *colnames,
         *colheadings[2],
         *allowed = " ",
          *fileauths = "*READ     *OBJOPR   *EXECUTE  ";
    
    _RFILE *fp;
    _RIOFB_T *fb;
    unsigned long rrn [LISTSIZE];

    Qdb_Qdbfh_t      *fddata;
    Qdb_Qdbfphys_t   *pfdata;
    Qdb_Qdbflogl_t   *lfdata;
    int fdlen = 50000;
    
    /* qusrmbrd-related */
    Qdb_Mbrd0200_t *mbrdata;
    mbrnames *pmbrnames;
    int mbr_array_exists = false,
        dbr_array_exists = false;
    char selectedMember [OBJSIZE] = {"          "};

    Qus_OBJD0100_t objinfo;

    /* for userspace */
    char extattr[10];
    char ustext [50] = "EOs tabkoll userspace";
    char *hdr_section,
         *list_section;

    /* qdbrtvd       */
    char qrfilename [20];
    char system     [10] = "*LCL      ";

    /* for qusrjobi  */
    char qualjobname[26];
    char intjobid   [16];
    Qwc_JOBI0400_t jobinfo;

    /* db-relations  */
    dbrinfo_t *dbrdata;
    int        nbrDBRs;

    /* DSM          */
    Qsn_Env_Desc_T  env_desc = {QSN_ENV_SAME, QSN_ENV_SAME, QSN_ENV_SAME,
                                QSN_ENV_SAME, QSN_COEXIST_NO, QSN_ENV_SAME, "*REQUESTER"
                               };
    Qsn_Ssn_T       colsession;
    Qsn_Ssn_T       DDLsession;
    Qsn_Win_T       mbrwin = 0,
                    dbrwin = 0;
    Q_Fdbk_T        dsmerr = {sizeof(Q_Fdbk_T)};

    struct lconv *curlocale;

    if (argc != 3)
    {
        fprintf(stderr, "Wrong nbr of parameters: %d!\n", argc);
        return 0;
    }

    /* transform argv[1] to file lib-form                                              */
    snprintf(filename, sizeof(filename), "%-10.10s%-10.10s", argv[1], argv[1] + 10);
    memcpy(mbrname, argv[2], sizeof(mbrname));

    /* get lengths for fil, lib och member without trailing blanks */
    pch = (char *) memchr((void *) argv[1], ' ', 10);
    arglen[0] = (pch == NULL ? 10 : pch - argv[1]);
    pch = (char *) memchr((void *) (argv[1] + 10), ' ', 10);
    arglen[1] = (pch == NULL ? 10 : pch - (argv[1] + 10));
    pch = (char *) memchr((void *) argv[2], ' ', 10);
    arglen[2] = (pch == NULL ? 10 : pch - argv[2]);

    /* verify that file exists     */
    err = (Qus_EC_t *) &error_code[0];
    memset(err, '0', sizeof(*err));
    err->Bytes_Provided = sizeof(error_code) - 1;
    QUSROBJD(&objinfo, sizeof(objinfo), "OBJD0100", filename, "*FILE     ", err);
    if (err->Bytes_Available > 0)
    {
        fprintf(stderr, "File %.*s/%.*s does not exist\n", 
        	arglen[1], argv[1] + 10, arglen[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    /* Verify that member exists and get its info           */
    mbrdata = (Qdb_Mbrd0200_t *) malloc(sizeof(Qdb_Mbrd0200_t));
    QUSRMBRD(mbrdata, sizeof(Qdb_Mbrd0200_t), "MBRD0200", filename, mbrname, "0", err);
    if (err->Bytes_Available > 0)
    {
        fprintf(stderr, "Member %.*s in file %.*s/%.*s does not exist\n", 
        	arglen[2], argv[2], arglen[1], argv[1] + 10, arglen[0], argv[1]);
        free(mbrdata);
        exit(EXIT_FAILURE);
    }
    memcpy(mbrname, mbrdata->Member_Name, OBJSIZE);

    /* use QSYCUSRA to check that user has *READ authority */
    QSYCUSRA(allowed, "*CURRENT  ", filename, "*FILE     ", (void *) fileauths, 
    	&nbrAuthorities, &j, err);
    if (memcmp(allowed, "N", 1) == 0)
    {
        fprintf(stderr, "User not authorised to file %.*s/%.*s\n", arglen[1], argv[1] + 10, 
        	arglen[0], argv[1]);
        exit(EXIT_FAILURE);
    }

    /* get detailed fileinfo with QDBRTVFD API. API does not support DDM files it seems  */
    fddata = (Qdb_Qdbfh_t *) malloc(fdlen);
    err->Bytes_Provided == sizeof(error_code) - 1;
    QDBRTVFD(fddata, fdlen, qrfilename, "FILD0100", filename, "*FIRST    ", "0",
             system, "*EXT      ", err);
    pfdata = (Qdb_Qdbfphys_t *) ((char *) fddata + fddata->Qdbpfof);
    lfdata = (Qdb_Qdbflogl_t *) ((char *) fddata + fddata->Qdblfof);
    jrndata = (Qdb_Qdbfjoal_t *) ((char *) fddata + fddata->Qdbfjorn);

    /* if file contains "complex" datatypes that we can't access without SQL, leave */
    if (fddata->Qdbfnbit.Qdbfhudt == 1 ||
            fddata->Qdbfnbit.Qdbfhlob == 1 ||
            fddata->Qdbfnbit.Qdbfhdtl == 1)
    {
        fprintf(stderr, "Table contains UDT, LOB or datalink fields.\nThese can only be accessed with SQL, so try that instead.\n");
        free(fddata);
        free(mbrdata);
        exit(EXIT_FAILURE);
    }


    /* if keyed access path, call qdbrtvd again with FILD0300 to get key info */
    if (fddata->Qdbfhflg.Qdbfhfky == 1)
    {
        short i;
        keydata = (Qdb_Qdbwh_t *) malloc(sizeof(Qdb_Qdbwh_t));
        QDBRTVFD(keydata, sizeof(Qdb_Qdbwh_t), qrfilename, "FILD0300", filename,
                 mbrname, "0", system, "*EXT      ", err);
        /* this is followed by a linked list per key               */
        keys = (Qdb_Qdbwhkey_t *) ((char *) keydata + keydata->Rec_Key_Info->Key_Info_Offset);
        keyfldinfo = 
        	(keystruct *) malloc(sizeof(keystruct) * keydata->Rec_Key_Info->Num_Of_Keys);

        for (i = 0; i < keydata->Rec_Key_Info->Num_Of_Keys; i++)
        {
            /* create list of keys     */
            memcpy(keyfldinfo[i].keyname, keys[i].Int_Field_Name, 
            	sizeof(keyfldinfo->keyname));
            keyfldinfo[i].Data_Type = keys[i].Data_Type;
            keyfldinfo[i].Field_Len = keys[i].Field_Len;
            keyfldinfo[i].Num_Of_Digs = keys[i].Num_Of_Digs;
            keyfldinfo[i].Dec_Pos = keys[i].Dec_Pos;
            keylen += keys[i].Field_Len;
        }
    }

    /* if there are null capable fields, open to handle this   */
    sprintf(fileflags, "%s", fddata->Qaaf2.Qdbfnfld == 1 ? "rr, nullcap=Y" : 
    	"rr");    

    /* file is opened in lib/file form    */
    sprintf(qualfilename, "%.*s/%.*s(%.*s)", arglen[1], argv[1] + 10, arglen[0], 
    	argv[1], arglen[2], argv[2]);

    errno = 0;
    if ((fp = _Ropen(qualfilename, fileflags)) == NULL)
    {
        fprintf(stderr, "%s: Open failed\n", strerror(errno));
        free(fddata);
        free(mbrdata);
        exit(EXIT_FAILURE);
    }

    /* create UserSpace                                                              */
    memset(extattr, ' ', sizeof(extattr));
    QUSCRTUS(usname, extattr, 65535, " ", "*CHANGE   ", ustext, "*YES      ", err);

    /* get info about table columns                                                 */
    QUSLFLD(usname, "FLDL0100", filename, "*FIRST    ", "0", err);
    QUSPTRUS(usname, &space, err);

    /* get info about the fields                                                    */
    char_space = (char *)space;
    list_section = char_space;
    hdr_section = list_section + space->Offset_Header_Section;
    field_hdr = (Qdb_Lfld_Header_t *) hdr_section;
    list_section = list_section + space->Offset_List_Data;
    field_list = (Qdb_Lfld_FLDL0100_t *) list_section;

    fptr = malloc(sizeof(fielddata_t) * space->Number_List_Entries);
    for (fieldno = 0; fieldno < space->Number_List_Entries; fieldno++)
    {
        memcpy(fptr[fieldno].Field_Name, field_list->Field_Name,
               sizeof(field_list->Field_Name));
        fptr[fieldno].Data_Type = field_list->Data_Type;
        fptr[fieldno].Output_Buffer_Position = field_list->Output_Buffer_Position;
        fptr[fieldno].Field_Length_Bytes = field_list->Field_Length_Bytes;
        fptr[fieldno].Digits = field_list->Digits;
        fptr[fieldno].Decimal_Positions = field_list->Decimal_Positions;
        memcpy(fptr[fieldno].Column_Heading, field_list->Column_Heading1,
               sizeof(field_list->Column_Heading1));
        memcpy(fptr[fieldno].Column_Heading + 20, field_list->Column_Heading2,
               sizeof(field_list->Column_Heading2));
        fptr[fieldno].Var_Fld_Ind = field_list->Variable_Length_Field_Ind;
        fptr[fieldno].nbr_DBCS_chars = field_list->Number_DBCS_Characters;
        fptr[fieldno].data_CCSID = field_list->Field_Data_CCSID;
        fptr[fieldno].fldsize = 0;

        switch (field_list->Data_Type)
        {
            case 'A':
            case 'T':
            case 'L':
            case 'S':
            case 'Z':
                /* if varchar then the first 2 bytes are a short with the length       */
                if (fptr[fieldno].Data_Type == 'A' && fptr[fieldno].Var_Fld_Ind == '1')
                {
                    fptr[fieldno].dspsize = (field_list->Field_Length_Bytes - 2) < 10 ?
                                            10 : (field_list->Field_Length_Bytes - 2);
                    fptr[fieldno].fldsize = field_list->Field_Length_Bytes - 2;
                }
                else
                {
                    fptr[fieldno].dspsize = field_list->Field_Length_Bytes < 10 ?
                                            10 : field_list->Field_Length_Bytes;
                    fptr[fieldno].fldsize = field_list->Field_Length_Bytes;
                }
                break;

            case 'B':
                if (field_list->Digits < 9)
                {
                    fptr[fieldno].dspsize = 10;
                }
                else
                {
                    fptr[fieldno].dspsize = field_list->Digits + 1;    /* signed */
                }
                break;

            case 'P':
                if (field_list->Digits < 9)
                {
                    fptr[fieldno].dspsize = 10;
                }
                else if (field_list->Digits >= 9 && field_list->Decimal_Positions > 0)
                {
                    fptr[fieldno].dspsize = field_list->Digits + 2;  /* space for decpointen and sign */
                }
                else
                {
                    fptr[fieldno].dspsize = field_list->Digits + 1;    /* signed */
                }
                break;

            case 'F':
                if (field_list->Digits < 9)
                {
                    fptr[fieldno].dspsize = 10;
                }
                else if (field_list->Digits >= 9 && field_list->Decimal_Positions > 0)
                {
                    fptr[fieldno].dspsize = field_list->Digits + 2; 
                }
                break;

#if (__OS400_TGTVRM__ >= 610)
            case '6': /* decimal floating point */
                fptr[fieldno].dspsize = field_list->Digits + 1;
                break;
#endif

            case 'H': /* hexadecimal takes twice the space */
                {
                    fptr[fieldno].dspsize = field_list->Field_Length_Bytes  * 2 < 10 ?
                                            10 : field_list->Field_Length_Bytes * 2;
                    fptr[fieldno].fldsize = field_list->Field_Length_Bytes * 2;
                }
                break;

            case 'G':
                fptr[fieldno].dspsize = field_list->Number_DBCS_Characters;
                break;
        }

        /* numerical fldsize. space for decpoint and optionally signed value (-) */
        if (fptr[fieldno].fldsize == 0)
            if (field_list->Decimal_Positions > 0)
            {
                fptr[fieldno].fldsize = field_list->Digits + 2;
            }
            else
            {
                fptr[fieldno].fldsize = field_list->Digits + 1;
            }

        /* string containing datatype and length formatted */
        memset(fptr[fieldno].dataTypeString, ' ', sizeof((fielddata_t *)0)->dataTypeString);
        if (field_list->Digits == 0) /* alfa typ */
        {
            snprintf(fptr[fieldno].dataTypeString, sizeof((fielddata_t *)0)->dataTypeString, 
            	"%c%c %5d       ", fptr[fieldno].Data_Type, 
            	fptr[fieldno].Var_Fld_Ind == '1' ? 'V' : ' ',
				field_list->Field_Length_Bytes);
        }
        else // ha float o decfloat f�r sig en�r inga dec positions?
        {
            snprintf(fptr[fieldno].dataTypeString, sizeof((fielddata_t *)0)->dataTypeString, 
            	"%c%c    %2d,%2d    ", fptr[fieldno].Data_Type,
				fptr[fieldno].Var_Fld_Ind == '1' ? 'V' : ' ', fptr[fieldno].Digits,
            	fptr[fieldno].Decimal_Positions);
        }

        if (fptr[fieldno].fldsize > maxfldsize)
        {
            maxfldsize = fptr[fieldno].fldsize;
        }

        fptr[fieldno].strpos = dspreclen;
        dspreclen += fptr[fieldno].dspsize + 1;   /* space between fields */
        field_list = (Qdb_Lfld_FLDL0100_t *) ((char *)
                                              field_list + space->Size_Each_Entry);
    }

    dspreclen -= 1;  /* no space after last column */
    loadColumnNames(&colnames, colheadings, dspreclen);
    nbrColumns = space->Number_List_Entries;

    /* use QUSRJOBI with JOBI0400 to get CCSID and decpoint of the current job */
    memset(qualjobname, ' ', sizeof(qualjobname));
    memcpy(qualjobname, "*", 1);
    memset(intjobid, ' ', sizeof(intjobid));
    QUSRJOBI(&jobinfo, sizeof(jobinfo), "JOBI0400", qualjobname, intjobid, err);
    decpoint = (memcmp(jobinfo.Decimal_Format, " ", 1) == 0 ? '.' : ',');

    /* a DSM low level environment might save some performance */
    envhandle = QsnCrtEnv(&env_desc, 16, NULL, 0, &envhandle, NULL);

    /* create and empty DSM output buffer.    */
    cmdbuf = QsnCrtCmdBuf(2500, 50, 0, NULL, NULL);
    inputbuf = QsnCrtInpBuf(50, 10, 0, NULL, NULL);
    QsnClrBuf(cmdbuf, NULL);
    QsnClrBuf(inputbuf, NULL);
    QsnClrScr('4', 0, envhandle, &dsmerr);
    if (dsmerr.available > 0)
    {
        fprintf(stderr, "API QsnClrScr failed with error %s\n", dsmerr.exception_id);
        exit(EXIT_FAILURE);
    }

    /* create the silly journals window, display when asked for it. 
       should be replaced by a "session" ideally (see below)  */
    QsnInzWinD(&win_desc, win_desc_length, NULL);
    win_desc.GUI_support = '0';
    setWindowArea(&win_desc, 10, 10, 10, 35);
    ext.draw_fp = drawJrnWindow;
    jrnwin = QsnCrtWin(&win_desc, win_desc_length, &ext, sizeof(ext),
                       '0', NULL, 0, NULL, NULL);

    /* Session (window) with columninfo                                             */
    loadColDataSession(&colsession, nbrColumns);

    if (fddata->Qdbfhflg.Qdbfhfky == 1)
    {
        win_desc.fullscreen = '1';
        keywin = QsnCrtWin(&win_desc, win_desc_length, NULL, 0, '0', NULL, 0, NULL, NULL);
    }

    loadHdr(fddata, pfdata, lfdata, mbrdata, colnames, colheadings, dspreclen, dspcolnames);
    loadFooter();
    listtop = true;

    /* memory for the list (subfile-ish) but don't bother to check mallocs return... */
    for (i = 0; i < LISTSIZE; i++)
    {
        listData[i] = malloc(dspreclen);
    }

    loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, strlistcol, dspreclen, 
    	decpoint);
    QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0, cmdbuf, envhandle, NULL);
    QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
    pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    while (pinput->AID != QSN_F3)
    {
        Q_Bin4 csrrow = 0;

        QsnClrBuf(cmdbuf, NULL);
        QsnGetCsrAdr(&csrrow, NULL, envhandle, NULL);  /* if user wants to see single row, which one? */
        if (pinput->AID == QSN_ENTER)
        {
            len = (QsnRtvDtaLen(inputbuf, NULL, NULL) - 3);
            if (len > 0)
            {
                if (memcmp(pinput->field, "B", 1) == 0 && listend == false)
                {
                    _Rlocate(fp, NULL, 0, __END);
                    listrrn = 1;
                    pageUp(fp, fb, &listtop, &listend);
                    loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, strlistcol, 
                    	dspreclen, decpoint);
                    listtop = false;
                }
                else if (memcmp(pinput->field, "T", 1) == 0 && listtop == false)
                {
                    _Rlocate(fp, NULL, 0, __START);
                    pageDown(&listtop);
                    loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, strlistcol, 
                    	dspreclen, decpoint);
                    listend = false;
                }
                else if (memcmp(pinput->field, "RRN", 3) == 0 && memcmp(fddata->Qdbfpact, "AR", 2) == 0)
                {
                    posrrn = atoi(pinput->field + 3);
                    if (posrrn > 0)
                    {
                        fb = _Rlocate(fp, NULL, posrrn, __RRN_EQ);
                        if (fb->num_bytes == 1)
                        {
                            _Rlocate(fp, NULL, 0, __PREVIOUS);
                            pageDown(&listtop);
                            loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, 
                            	strlistcol, dspreclen, decpoint);
                        }
                    }
                }
                else if (memcmp(pinput->field, "W", 1) == 0)
                {
                    wpos = atoi(pinput->field + 1);
                    if (wpos <= dspreclen && wpos >= 0)
                    {
                        int datalen = handleRightmostView(LISTSTRROW - 1, listw, 
                        	strlistcol, dspreclen);
                        if (datalen < listw)
                        {
                            if (padWithSpaces(LISTSTRROW - 2, LISTSTRROW - 2, 
                            	strlistcol + datalen, listw - datalen) == 1)
                                ;
                        }
                        writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, 
                        	wpos, datalen); 
                        shiftView(listw, strlistcol, dspreclen, rrn);
                    }
                    else
                    {
                        wpos = 0;
                    }
                }
                else if (memcmp(pinput->field, "K", 1) == 0 && fddata->Qdbfhflg.Qdbfhfky == 1)
                {
                    if (displayKeyWindow(fp, fb, keylen) == 1)
                    {
                        pageDown(&listtop);
                        loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, 
                        	strlistcol, dspreclen, decpoint);
                    }
                }
                else if (memcmp(pinput->field, "DBR", 3) == 0)
                {
                    char sltfile[FILELIB_SIZE];

                    if (dbr_array_exists == false)
                    {
                        if (createDBRList(&qrfilename[0], &usname[0], &dbrdata, &nbrDBRs) == 1)
                        {
                            dbr_array_exists = true;
                        }
                    }
                    /* display DBR data */
                    if (displayDBRWindow(&dbrwin, dbrdata, sltfile, &qrfilename[0], nbrDBRs) == 1)
                    {
                        /* can't handle opening of new file yet */
                    }
                }
                else if (memcmp(pinput->field, "MBR", 3) == 0 && fddata->Qdbfhmnum > 1)
                {
                    if (mbr_array_exists == false)
                    {
                        if (createMemberList(&qrfilename[0], &usname[0], &pmbrnames) == 1)
                        {
                            mbr_array_exists = true;
                        }
                    }
                    if (displayMemberWindow(&mbrwin, pmbrnames, selectedMember, 
                    	&qrfilename[0], fddata->Qdbfhmnum, jobinfo.Coded_Char_Set_ID) == 1)
                    {
                        QsnClrScr('4', 0, envhandle, NULL);
                        _Rclose(fp);
                        memcpy(mbrname, selectedMember, sizeof(selectedMember));
                        QUSRMBRD(mbrdata, sizeof(Qdb_Mbrd0200_t), "MBRD0200", filename,
                                 mbrname, "0", err);
                        sprintf(qualfilename, "%.*s/%.*s(%.*s)", arglen[1], argv[1] + 10,
                                arglen[0], argv[1], 10, mbrname);

                        errno = 0;
                        if ((fp = _Ropen(qualfilename, fileflags)) == NULL)
                        {
                            fprintf(stderr, "Open failed\n");
                            cleanUp();
                            exit(EXIT_FAILURE);
                        }
                        loadHdr(fddata, pfdata, lfdata, mbrdata, colnames, colheadings, 
                        	dspreclen, dspcolnames);
                        loadFooter();
                        loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, 
                        	strlistcol, dspreclen, decpoint);
                        displayRRN = false;
                    }
                }
            }
        }
        else if (pinput->AID == QSN_PAGEDOWN)
        {
            if (listend == false)
            {
                pageDown(&listtop);
                loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, strlistcol, 
                	dspreclen, decpoint);
            }
        }
        else if (pinput->AID == QSN_PAGEUP)
        {
            if (listtop == false)
            {
                pageUp(fp, fb, &listtop, &listend);
                loadlist(fp, fb, fddata, &listend, nbrColumns, rrn, listw, strlistcol, 
                	dspreclen, decpoint);
            }
        }
        /* shift right */
        else if (pinput->AID == QSN_F20 && dspreclen > listw + wpos)
        {
            int datalen = 0;
            for (i = 0; i < nbrColumns; i++)
            {
                if (fptr[i].strpos > listw + wpos || i == nbrColumns - 1)
                {
                    if (fptr[i].strpos > listw + wpos)
                        /* start with the column that is cut off in current view    */
                    {
                        wpos = fptr[i - 1].strpos;
                    }
                    else
                    {
                        wpos = fptr[i].strpos;
                    }

                    datalen = handleRightmostView(LISTSTRROW - 1, listw, strlistcol, dspreclen);
                    if (datalen < listw)
                    {
                        if (padWithSpaces(LISTSTRROW - 2, LISTSTRROW - 2, 
                        	strlistcol + datalen, listw - datalen) == 1)
                            ;
                    }
                    writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, wpos, 
                    	datalen);
                    break;
                }
            }
            shiftView(listw, strlistcol, dspreclen, rrn);
        }
        /* Shift left. handle that user can have used the W option to shift view */
        else if (pinput->AID == QSN_F19 && wpos > 0)
        {
            int datalen = 0;
            if (wpos - listw <= 0)
            {
                wpos = 0;
            }
            else
            {
                if (nbrColumns > 1)
                {
                    for (i = nbrColumns - 1; i >= 0; i--)
                    {
                        if (fptr[i].strpos < wpos - listw)
                        {
                            wpos = fptr[i + 1].strpos;
                            break;
                        }
                    }
                }
                else
                {
                    wpos -= listw;
                    if (wpos < 0)
                    {
                        wpos = 0;
                    }
                }
            }
            datalen = handleRightmostView(LISTSTRROW - 1, listw, strlistcol, dspreclen);
            writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, wpos, datalen);
            shiftView(listw, strlistcol, dspreclen, rrn);
        }
        /* Alternate between column names och headings */
        else if (pinput->AID == QSN_F8)
        {
            int datalen;
            if (dspcolnames == true)
            {
                dspcolnames = false;
            }
            else
            {
                dspcolnames = true;
                QsnWrtPad(' ', listw, 0, LISTSTRROW - 2, strlistcol, cmdbuf, envhandle, 
                	NULL);
            }
            datalen = handleRightmostView(LISTSTRROW - 1, listw, strlistcol, dspreclen);
            if (datalen < listw)
            {
                if (padWithSpaces(LISTSTRROW - 2, LISTSTRROW - 2, strlistcol + datalen, 
                	listw - datalen) == 1)
                    ;
            }
            writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, wpos, 
            	datalen - 1); 
        }
        /* Single record  */
        else if (pinput->AID == QSN_F11)
        {        
            if (csrrow < LISTSTRROW || csrrow >= LISTSTRROW + LISTSIZE)
            {
                csrrow = LISTSTRROW;
            }

            /* should only be created once if I'd known better */
            win_desc.fullscreen = '1';
            sngrowwin = QsnCrtWin(&win_desc, win_desc_length, NULL, 0,
                                  '0', NULL, 0, NULL, NULL);
            dspSingleRow(csrrow, maxfldsize, nbrColumns, mbrname);
        }
        else if (pinput->AID == QSN_F21)
        {
            QUSCMDLN();
        }
        else if (pinput->AID == QSN_F22)
        {
            if (displayRRN == false)
            {
                char s [11];
                int row = LISTSTRROW;
                int datalen;

                displayRRN = true;
                listw = LISTWIDTH - RRNCOLSIZE;
                strlistcol = LISTSTRCOL + RRNCOLSIZE;

                datalen = handleRightmostView(LISTSTRROW - 1, listw, strlistcol, dspreclen);
                writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, wpos, 
                	datalen);
                shiftView(listw, strlistcol, dspreclen, rrn);

                if (dspcolnames == false)
                {
                    QsnWrtPad(' ', sizeof(s), 0, LISTSTRROW - 2, LISTSTRCOL, cmdbuf, 
                    	envhandle, NULL);
                }
                snprintf(s, sizeof(s), "%s", "*RRN      ");
                QsnWrtDta(s, strlen(s), 0, LISTSTRROW - 1, LISTSTRCOL, mcattr, mcattr, 
                	white, white, cmdbuf, envhandle, NULL);
            }
            else
            {
                int datalen;

                displayRRN = false;
                listw = LISTWIDTH;
                strlistcol = LISTSTRCOL;
                datalen = handleRightmostView(LISTSTRROW - 1, listw, strlistcol, dspreclen);
                writeColumnNames(colnames, colheadings, dspcolnames, strlistcol, wpos, datalen);
                shiftView(listw, strlistcol, dspreclen, rrn);
            }
        }
        else if (pinput->AID == QSN_F13)
        {
            displayWindow(jrnwin);
        }
        else if (pinput->AID == QSN_F14)
        {
            displaySession(colsession);
        }
        else if (pinput->AID == QSN_F2)
        {
            if (generateDDL(arglen[0], filename, &DDLsession, fddata, lfdata) > 0)
            {
                displaySession(DDLsession);
            }
        }

        QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
                   cmdbuf, envhandle, NULL);
        QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
        pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    }

    _Rclose(fp);
    free(colnames);
    free(colheadings[0]);
    free(colheadings[1]);
    if (fddata->Qdbfhflg.Qdbfhfky == 1)
    {
        free(keydata);
        free(keyfldinfo);
    }
    free(fddata);
    free(mbrdata);
    if (mbr_array_exists == true)
    {
        free(pmbrnames);
    }
    if (dbr_array_exists == true)
    {
        free(dbrdata);
    }
    cleanUp();
    return 0;
}

/*
* functions
*/
void loadColumnNames(char **colnames, char **colheadings, int dspreclen)
{
    short i, pos, start = 0;

    *colnames = malloc((dspreclen + 1));
    memset(*colnames, ' ', dspreclen);
    colheadings[0] = malloc(dspreclen + 1);
    colheadings[1] = malloc(dspreclen + 1);
    memset(colheadings[0], ' ', dspreclen);
    memset(colheadings[1], ' ', dspreclen);
    for (i = 0; i < space->Number_List_Entries; i++)
    {
        short headinglen = 0;

        pos = fptr[i].dspsize < 10 ? 10 : fptr[i].dspsize;
        memcpy(*colnames + start, fptr[i].Field_Name, 10);
        /* fiddle in colheadings into it's two arrays */
        if (fptr[i].dspsize >= 40)
        {
            memcpy(colheadings[0] + start, fptr[i].Column_Heading, 40);
            memset(colheadings[1] + start, ' ', fptr[i].dspsize);
        }
        else
        {            
            short j = fptr[i].dspsize;
            char s[41];

            memcpy(s, fptr[i].Column_Heading, sizeof(s) - 1);
            s[40] = '\0';
            headinglen = triml(s, ' ');
            if (headinglen > fptr[i].dspsize && headinglen <= (fptr[i].dspsize * 2) ) /* linebreak */
            {
                while (j > 0)
                {
                    if (isspace(fptr[i].Column_Heading[j]) )
                    {
                        if (headinglen - j < fptr[i].dspsize)
                        {
                            /* break line on word level */
                            memcpy(colheadings[0] + start, fptr[i].Column_Heading, j);
                            memcpy(colheadings[1] + start, fptr[i].Column_Heading + j + 1, 
                            	headinglen - j + 1);
                        }
                        else
                            /* break line hard on dspsize */
                        {
                            memcpy(colheadings[0] + start, fptr[i].Column_Heading, 
                            	fptr[i].dspsize);
                            memcpy(colheadings[1] + start, 
                            	fptr[i].Column_Heading + fptr[i].dspsize,
								fptr[i].dspsize > sizeof(fptr[i].Column_Heading) - 
								fptr[i].dspsize ? sizeof(fptr[i].Column_Heading) - 
								fptr[i].dspsize : fptr[i].dspsize);
                        }
                        break;
                    }
                    j--;
                }
            }
            else
            {
                memcpy(colheadings[0] + start, fptr[i].Column_Heading, fptr[i].dspsize);
                memcpy(colheadings[1] + start, fptr[i].Column_Heading + fptr[i].dspsize,
                       fptr[i].dspsize > sizeof(fptr[i].Column_Heading) - fptr[i].dspsize 
                       ? sizeof(fptr[i].Column_Heading) - fptr[i].dspsize :
                       fptr[i].dspsize);
            }
        }
        start += (pos + 1);
        memcpy(*colnames + (start - 1), "|", 1);
        memcpy(colheadings[0] + start - 1, "|", 1);
        memcpy(colheadings[1] + start - 1, "|", 1);
    }
    memcpy(*colnames + dspreclen, "|", 1);
    memcpy(colheadings[0] + dspreclen, "|", 1);
    memcpy(colheadings[1] + dspreclen, "|", 1);
}


void writeColumnNames(char *colnames, char **colheadings, int dspcolnames, int strlistcol, 
	int wpos, int datalen)
{
    if (datalen == LISTWIDTH)
    {
        datalen -= 1;
    }
    /* underline is not showed unless you "do something", eg shift view with "w0" or so */
    if (dspcolnames == true)
    {
        QsnWrtDta(colnames + wpos, datalen, 0, LISTSTRROW - 1, strlistcol, norm, norm,
                  white, white, cmdbuf, envhandle, NULL);
    }
    else
    {
        QsnWrtDta(colheadings[0] + wpos, datalen, 0, LISTSTRROW - 2, strlistcol, mcattr, 
        	mcattr, white, white, cmdbuf, envhandle, NULL);
        QsnWrtDta(colheadings[1] + wpos, datalen, 0, LISTSTRROW - 1, strlistcol, mcattr, 
        	mcattr, white, white, cmdbuf, envhandle, NULL);
    }
}


void loadHdr(Qdb_Qdbfh_t    *fddata,
             Qdb_Qdbfphys_t *pfdata,
             Qdb_Qdbflogl_t *lfdata,
             Qdb_Mbrd0200_t *mbrdata,
             char           *colnames,
             char          **colheadings,
             int             dspreclen,
             int             dspcolnames)
{
    char headtext [LISTWIDTH];
    char s [19];
    int row, column, i;
    _MMTR_Template_T machine_attributes;

    snprintf(headtext, LISTWIDTH, "%s", " Table-koll ");
    row = 1;
    column = 57;
    mcattr = QSN_SA_HI;

    QsnWrtDta(headtext, strlen(headtext), 0, row, column, mcattr, mcattr, QSN_SA_PNK_RI, 
    	norm, cmdbuf, envhandle, NULL);

    /* File- and library name                                                   */
    row = 2;
    column = 2;
    QsnWrtDta("File:    ", 9, 0, row, column, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);

    column = 11;
    QsnWrtDta(field_hdr->File_Name_Used, 10, 0, row, column, mcattr, mcattr, white, white, 
    	cmdbuf, envhandle, NULL);

    QsnWrtDta(mbrdata->Text_Desc, 50, 0, row, 24, mcattr, mcattr, white, white, cmdbuf, 
    	envhandle, NULL);

    row = 3;
    column = 2;
    QsnWrtDta("Library: ", 9, 0, row, column, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);

    column = 11;
    QsnWrtDta(field_hdr->Library_Name, 10, 0, row, column, mcattr, mcattr, white, white, 
    	cmdbuf, envhandle, NULL);

    row = 4;
    column = 2;
    QsnWrtDta("Member:  ", 9, 0, row, column, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);

    QsnWrtDta(mbrdata->Member_Name, 10, 0, row, 11, mcattr, mcattr, white, white, cmdbuf, 
    	envhandle, NULL);

    row = 3;
    column = 22;
    QsnWrtDta("File type: ", 11, 0, row, column, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);

    column = 33;
    QsnWrtDta(field_hdr->File_Type, 10, 0, row, column, mcattr, mcattr, white, white, 
    	cmdbuf, envhandle, NULL);

    row = 4, column = 22;
    QsnWrtDta("Access:    ", 11, 0, row, column, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);

    column = 33;
    QsnWrtDta(fddata->Qdbfpact, 2, 0, row, column, mcattr, mcattr, white, white, cmdbuf, 
    	envhandle, NULL);

    QsnWrtDta("Nbr records:", 12, 0, 3, 36, mcattr, mcattr, pink, pink, cmdbuf, envhandle, 
    	NULL);

    sprintf(s, "%d", mbrdata->Num_Cur_Rec);
    QsnWrtDta(s, strlen(s), 0, 3, 49, mcattr, mcattr, white, white, cmdbuf, envhandle, 
    	NULL);

    QsnWrtDta("Rec length :", 12, 0, 4, 36, mcattr, mcattr, pink, pink, cmdbuf, envhandle, 
    	NULL);

    sprintf(s, "%d", fddata->Qdbfmxrl);
    QsnWrtDta(s, strlen(s), 0, 4, 49, mcattr, mcattr, white, white, cmdbuf, envhandle, 
    	NULL);

    QsnWrtDta("Nbr members:", 12, 0, 3, 59, mcattr, mcattr, pink, pink, cmdbuf, envhandle, 
    	NULL);
    sprintf(s, "%d", fddata->Qdbfhmnum);
    QsnWrtDta(s, strlen(s), 0, 3, 72, mcattr, mcattr,white, white, cmdbuf, envhandle, 
    	NULL);

    QsnWrtDta("Max members:", 12, 0, 4, 59, mcattr, mcattr, pink, pink, cmdbuf, envhandle, 
    	NULL);	
    if (fddata->Qdbfhmxm == 0)
    {
        sprintf(s, "32767");
    }
    else
    {
        sprintf(s, "%d", fddata->Qdbfhmxm);
    }
    column = 72;
    QsnWrtDta(s, strlen(s), 0, 4, column, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);

    /* should handle MQTs, Joinfiles etc */
    QsnWrtDta("SQL type:", 9, 0, 3, column + 6, mcattr, mcattr, pink, pink, cmdbuf, 
    	envhandle, NULL);
    if (fddata->Qdbfhflg.Qdbfhfpl == 0)  /* physical file                          */
    {
    	sprintf(s, "%s", pfdata->Qflags.Qdbfsqlt == 1 ? "Table" : "None");        

        QsnWrtDta(s, strlen(s), 0, 3, column + 16, mcattr, mcattr,
                  white, white, cmdbuf, envhandle, NULL);
        QsnWrtDta("Nbr triggers:", 13, 0, 4, column + 6, mcattr, mcattr,
                  pink, pink, cmdbuf, envhandle, NULL);
        sprintf(s, "%d", pfdata->Qdbftrgn);
        QsnWrtDta(s, strlen(s), 0, 4, column + 20, mcattr, mcattr, white, white, cmdbuf, 
        	envhandle, NULL);
    }
    else /* logical file                                                             */
    {
        if (lfdata->Qlfa.Qdbfsqlv == 1)
        {
            sprintf(s, "View");
        }
        else if (lfdata->Qlfa.Qdbfsqli == 1)
        {
            sprintf(s, "Index");
        }
        else
        {
            sprintf(s, "None");
        }
        QsnWrtDta(s, strlen(s), 0, 3, column + 16, mcattr, mcattr,
                  white, white, cmdbuf, envhandle, NULL);
        if (lfdata->Qlfa.Qdbfsqlv == 0 && lfdata->Qlfa.Qdbfsqli == 0) /* logical, non SQL file */
        {
            QsnWrtDta("Select/omit:", 12, 0, 4, column + 6, mcattr, mcattr, pink, pink, 
            	cmdbuf, envhandle, NULL);
            sprintf(s, "%s", fddata->Qdbfhflg.Qdbfkfso == 1 ? "Yes" : "No");	            
            QsnWrtDta(s, strlen(s), 0, 4, 78 + 13, mcattr, mcattr,
                      white, white, cmdbuf, envhandle, NULL);
        }
    }

    /* system info */
    machine_attributes.Options.Template_Size = 16;
    matmatr(&machine_attributes, _MMTR_APPN);
    column = 102;
    QsnWrtDta("System:", 7, 0, 3, column, mcattr, mcattr, blue, blue, cmdbuf, envhandle, 
    	NULL);
    snprintf(s, sizeof(s), "%8.8s", machine_attributes.Options.Data.APPN.Sys_Name);
    QsnWrtDta(s, strlen(s), 0, 3, column + 8, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);

    machine_attributes.Options.Template_Size = 14;
    matmatr(&machine_attributes, _MMTR_LIC_VRM);
    QsnWrtDta("Release:", 8, 0, 4, column, mcattr, mcattr, blue, blue, cmdbuf, envhandle, 
    	NULL);
    QsnWrtDta(machine_attributes.Options.Data.LicVRM.Lic_VRM, 6, 0, 4, column + 9, mcattr, 
    	mcattr, white, white, cmdbuf, envhandle, NULL);

    /* creation and Change_Date */
    QsnWrtDta("Created:", 8, 0, 5, 2, mcattr, mcattr, pink, pink, cmdbuf, envhandle, NULL);
    memcpy(s, mbrdata->Crt_Date[0] == '0' ? "19" : "20", 2);
    snprintf(s + 2, 19, "%2.2s-%2.2s-%2.2s %2.2s:%2.2s:%2.2s", mbrdata->Crt_Date + 1, 
    	mbrdata->Crt_Date + 3, mbrdata->Crt_Date + 5, mbrdata->Crt_Date + 7, 
    	mbrdata->Crt_Date + 9, mbrdata->Crt_Date + 11);
    QsnWrtDta(s, strlen(s), 0, 5, 11, mcattr, mcattr, white, white, cmdbuf, envhandle, NULL);

    QsnWrtDta("Changed:", 8, 0, 6, 2, mcattr, mcattr, pink, pink, cmdbuf, envhandle, NULL);
    memcpy(s, mbrdata->Change_Date[0] == '0' ? "19" : "20", 2);
    snprintf(s + 2, 19, "%2.2s-%2.2s-%2.2s %2.2s:%2.2s:%2.2s", mbrdata->Change_Date + 1, 
    	mbrdata->Change_Date + 3, mbrdata->Change_Date + 5, mbrdata->Change_Date + 7, 
    	mbrdata->Change_Date + 9, mbrdata->Change_Date + 11);
    QsnWrtDta(s, strlen(s), 0, 6, 11, mcattr, mcattr, white, white, cmdbuf, envhandle, NULL);

    /* headlines, names or headings */
    i = sizeof(headtext) < dspreclen + 1 ? sizeof(headtext) : dspreclen + 1;
    column = LISTSTRCOL;
    if (dspcolnames == true)
    {
        row = LISTSTRROW - 1;
        memcpy(headtext, colnames, i);
        QsnWrtDta(headtext, i, 0, row, column, mcattr, mcattr, white, white, cmdbuf, 
        	envhandle, NULL);
    }
    else /* columheadings */
    {
        row = LISTSTRROW - 2;
        QsnWrtDta(colheadings[0], i, 0, row, column, mcattr, mcattr, white, white, cmdbuf, 
        	envhandle, NULL);
        QsnWrtDta(colheadings[1], i, 0, row + 1, column, mcattr, mcattr, white, white, 
        	cmdbuf, envhandle, NULL);
    }

    row = 1;
    column = 2;
    QsnWrtDta("Input: ", strlen("Input: "), 0, row, column, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);

    column = 10;
    QsnSetFld(0, 20, row, column, QSN_FFW_AUTO_MONOCASE, NULL, 0,
              QSN_SA_UL, QSN_SA_WHT_UL, cmdbuf, envhandle, NULL);
}


void loadFooter()
{
    char *fkeys = "F3 Exit F2 Generate SQL F8 ColNames/Headings F11 Single row F14 Display Column Info F21 Cmd F22 RRN on/off";
    int row = 26,
        column = 2;

    QsnWrtDta(fkeys, strlen(fkeys), 0, row, column, mcattr, mcattr, blue, norm, cmdbuf, 
    	envhandle, NULL);
}


void loadlist(_RFILE *fp,
              _RIOFB_T *fb,
              Qdb_Qdbfh_t *fddata,
              int *listend,
              int nbrColumns,
              unsigned long *rrn,
              int listw,
              int strlistcol,
              int dspreclen,
              char decpoint)
{
    int row = LISTSTRROW,
        dspstart,
        fstart,
        n = 0,
        p = 0,
        k;
    int             *intp;
    short           *shortp,
                    decpos,
                    i;
    long long       *longlongp;
    void            *voidptr;
    float           *floatp;
    unsigned char   *zzoned,
             *vzoned;
#if (__OS400_TGTVRM__ >= 610)
    union
    {
        _Decimal32    *dfp32;
        _Decimal64    *dfp64;
        _Decimal128   *dfp128;
    } dfp;
#endif
    char listLine[16];
    char *inbuf = (char *) *fp->in_buf;

    listrrn = 0;
    /* read file in locate mode and fill array with list values */
    fb = _Rreadn(fp, NULL, 0, __DFT | __NO_LOCK);
    while (fb->num_bytes != EOF && listrrn < LISTSIZE)
    {
        dspstart = fstart = 0;
        memset(listData[listrrn], ' ', dspreclen);
        rrn[listrrn] = fb->rrn;

        /* convert to char in order to display  */
        for (i = 0; i < nbrColumns; i++)
        {
            if (fddata->Qaaf2.Qdbfnfld == 1)  /* Nullcapable columns            */
            {
                if (fp->in_null_map[i] == '1') /* field is NULL                 */
                {
                    memcpy(listData[listrrn] + dspstart, "-", strlen("-"));
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    continue;
                }
            }

            switch (fptr[i].Data_Type)
            {
                case 'A':
                case 'T':
                case 'L':
                case 'S':
                case 'Z':
                    /* varchar starts with a short */
                    if (fptr[i].Data_Type == 'A' && fptr[i].Var_Fld_Ind == '1')
                        memcpy(listData[listrrn] + dspstart, inbuf + fstart + 2,
                               (fptr[i].Field_Length_Bytes - 2));
                    else
                        memcpy(listData[listrrn] + dspstart, inbuf + fstart,
                               fptr[i].Field_Length_Bytes);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;

                case 'P':   /* packed decimal             */
                    n = fptr[i].Digits;
                    p = fptr[i].Decimal_Positions;
                    voidptr = malloc(fptr[i].Field_Length_Bytes);
                    memcpy(voidptr, inbuf + fstart, fptr[i].Field_Length_Bytes);
                    zzoned = malloc(fptr[i].Digits + 2); /* for digits == 9 we need 11 bytes for sign and extra pos for zoned sign. */
                    memset(zzoned, '0', sizeof(zzoned));
                    cpynv(NUM_DESCR(_T_ZONED,  n + 1, p + 1), zzoned + 1,
                          NUM_DESCR(_T_PACKED, n, p), voidptr);

                    decpos = fptr[i].Digits - (fptr[i].Decimal_Positions == 0 ? 
                    	fptr[i].Decimal_Positions : fptr[i].Decimal_Positions - 1);
                    k = 0;
                    while (zzoned[k] == '0' && k < (fptr[i].Decimal_Positions == 0 ? decpos : decpos - 1))
                    {
                        zzoned[k] = ' ';
                        k++;
                    }
                    /* Zoned fields last byte signifies negative value */
                    if (!isdigit(zzoned[fptr[i].Digits + 1]))
                    {
                        zzoned[k > 0 ? k - 1 : k] = '-';
                    }

                    if (fptr[i].Decimal_Positions > 0)  /* display decimal point       */
                    {
                        vzoned = malloc(fptr[i].Digits + 2);
                        memcpy(vzoned, zzoned, decpos);
                        vzoned[decpos] = decpoint;
                        memcpy(vzoned + decpos + 1,
                               zzoned + decpos,
                               fptr[i].Decimal_Positions);
                        sprintf(listData[listrrn] + dspstart, "%.*s", fptr[i].Digits + 2, 
                        	vzoned);
                        free(vzoned);
                    }
                    else  /* no dec point */
                    {
                        sprintf(listData[listrrn] + dspstart, "%.*s", fptr[i].Digits + 1, 
                        	zzoned);
                    }
                    free(zzoned);

                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    free(voidptr);
                    break;

                case 'B':  /* int */
                    voidptr = malloc(fptr[i].Field_Length_Bytes);
                    memcpy(voidptr, inbuf + fstart, fptr[i].Field_Length_Bytes);
                    if (fptr[i].Field_Length_Bytes == 4)
                    {
                        intp = (int *) voidptr;
                        sprintf(listData[listrrn] + dspstart, "%*d", fptr[i].Digits + 1, 
                        	*intp);
                    }
                    else if (fptr[i].Field_Length_Bytes == 2)
                    {
                        shortp = (short *) voidptr;
                        sprintf(listData[listrrn] + dspstart, "%*d", fptr[i].Digits + 1, 
                        	*shortp);
                    }
                    else if (fptr[i].Field_Length_Bytes == 8)
                    {
                        longlongp = (long long *) voidptr;
                        sprintf(listData[listrrn] + dspstart, "%*lld", fptr[i].Digits + 1,
                                *longlongp);
                    }
                    free(voidptr);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;

                case 'F':   /* floating point, not handled very nicely */
                    n = fptr[i].Digits;
                    p = fptr[i].Decimal_Positions;    /* unnecessary */
                    voidptr = malloc(fptr[i].Field_Length_Bytes);
                    memcpy(voidptr, inbuf + fstart, fptr[i].Field_Length_Bytes);
                    floatp = (float *) voidptr;
                    sprintf(listData[listrrn] + dspstart, "%f",  *floatp);
                    free(voidptr);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;

                case 'G': /* graphic, UTF-16 */
                    /* # says that precision is in nbr of characters regardless of size but writes ".ls" then */
                    snprintf(listData[listrrn] + dspstart, fptr[i].Field_Length_Bytes, 
                    	"%.*s", 100, inbuf + fstart);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;

                case 'H': /* hex */
                    cvthc(listData[listrrn] + dspstart, inbuf + fstart, fptr[i].dspsize);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;

                    /* decfloatingpoint if OS is at least 6.1 */
#if (__OS400_TGTVRM__ >= 610)
                case '6':
                    voidptr = malloc(fptr[i].Field_Length_Bytes);
                    memcpy(voidptr, inbuf + fstart, fptr[i].Field_Length_Bytes);
                    if (fptr[i].Field_Length_Bytes == 4)
                    {
                        dfp.dfp32 = (_Decimal32 *) voidptr;
                        sprintf(listData[listrrn] + dspstart, "%Hf", *dfp.dfp32);
                    }
                    else if (fptr[i].Field_Length_Bytes == 8)
                    {
                        dfp.dfp64 = (_Decimal64 *)voidptr;
                        sprintf(listData[listrrn] + dspstart, "%Df", *dfp.dfp64);
                    }
                    else  /* 16 bytes */
                    {
                        dfp.dfp128 = (_Decimal128 *)voidptr;
                        sprintf(listData[listrrn] + dspstart, "%DDf", *dfp.dfp128);
                    }
                    free(voidptr);
                    dspstart += fptr[i].dspsize + 1;
                    fstart += fptr[i].Field_Length_Bytes;
                    break;
#endif
            }
        }

        if (strlistcol > LISTSTRCOL) /* rrn mode */
        {
            snprintf(listLine, sizeof(listLine), "%10d", rrn[listrrn]);
            QsnWrtDta(listLine, 10, 0, row, LISTSTRCOL, norm, norm, pink, norm, cmdbuf, 
            	envhandle, NULL);
        }
        k = handleRightmostView(row, listw, strlistcol, dspreclen);
        QsnWrtDta(listData[listrrn] + wpos, k, 0, row, strlistcol, mcattr, mcattr,
                  blue, blue, cmdbuf, envhandle, NULL);
        fb = _Rreadn(fp, NULL, 0, __DFT | __NO_LOCK);
        listrrn += 1;
        row += 1;
    }

    if (fb->num_bytes != EOF)
    {
        fb = _Rreadp(fp, NULL, 0, __DFT | __NO_LOCK);
        strcpy(listLine, "There's more...");
    }
    else
    {
        strcpy(listLine, "End of file    ");
        *listend = true;
        _Rlocate(fp, NULL, 0, __END);
        fb = _Rreadp(fp, NULL, 0, __DFT | __NO_LOCK);
    }
    QsnWrtDta(listLine, strlen(listLine), 0, LISTSTRROW + LISTSIZE, 80, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);

    if (listrrn < LISTSIZE)
    {
        int tmp = listrrn;
        for (tmp; tmp < LISTSIZE; row++, tmp++)
        {
            memset(listData[listrrn], ' ', sizeof(listData));
            QsnWrtPad(' ', LISTWIDTH, 0, row, LISTSTRCOL, cmdbuf, envhandle, NULL);
        }
    }
    return;
}


void pageDown(int *listtop)
{
    *listtop = false;
}


void pageUp(_RFILE *fp, _RIOFB_T *fb, int *listtop, int *listend)
{
    int i;

    *listend = false;
    for (i = 1; i <= listrrn + LISTSIZE; i++)
    {
        fb = _Rreadp(fp, NULL, 0, __DFT | __NO_LOCK);
        if (fb->num_bytes == EOF)
        {
            _Rlocate(fp, NULL, 0, __START);
            *listtop = true;
            break;
        }
    }
}


void drawJrnWindow(const Qsn_Win_T *win, const Qsn_Cmd_Buf_T *cbuf)
{    
    char wdwtext1 [36];

    if (jrndata->Qdbfjact == '1')
    {
        snprintf(wdwtext1, sizeof(wdwtext1) - 1, "%s", "Journal name:");
        QsnWrtDta(wdwtext1, strlen(wdwtext1), 0, 1, 1, QSN_SA_HI, QSN_SA_HI,
                  red, norm, *cbuf, *win, NULL);

        QsnWrtDta(jrndata->Qdbfojrn, 10, 0, 1, 15, QSN_SA_HI, QSN_SA_HI,
                  white, norm, *cbuf, *win, NULL);

        snprintf(wdwtext1, sizeof(wdwtext1) - 1, "%s", "Journal lib:");
        QsnWrtDta(wdwtext1, strlen(wdwtext1), 0, 2, 1, QSN_SA_HI, QSN_SA_HI,
                  red, norm, *cbuf, *win, NULL);

        QsnWrtDta(jrndata->Qdbfolib, 10, 0, 2, 15, QSN_SA_HI, QSN_SA_HI,
                  white, norm, *cbuf, *win, NULL);

        snprintf(wdwtext1, sizeof(wdwtext1) - 1, "%s", "Last journaled date:");
        QsnWrtDta(wdwtext1, strlen(wdwtext1), 0, 4, 1, QSN_SA_HI, QSN_SA_HI,
                  red, norm, *cbuf, *win, NULL);

        QsnWrtDta(jrndata->Qdbfljrn + 1, 12, 0, 4, 22, QSN_SA_HI, QSN_SA_HI,
                  white, norm, *cbuf, *win, NULL);
    }
    else
    {
        snprintf(wdwtext1, sizeof(wdwtext1), "%s", "File not journaled");
        QsnWrtDta(wdwtext1, strlen(wdwtext1), 0, 5, 5, QSN_SA_HI, QSN_SA_HI,
                  pink, norm, *cbuf, *win, NULL);
    }
    snprintf(wdwtext1, sizeof(wdwtext1), "%s", "F4 Move F12 End window");
    QsnWrtDta(wdwtext1, strlen(wdwtext1), 0, -1, 1, QSN_SA_HI, QSN_SA_HI,
              red, norm, *cbuf, *win, NULL);
}


void loadColDataSession (Qsn_Ssn_T *colsession, int nbrColumns)
{
    storage_t       storage;
    ssnstorage_t    ssnstorage;
    char           *wintitle = "Column info";
    char           *fkeys1 = "F4 Move F17 Top F18 Bottom";
    Qsn_Win_Desc_T *win_desc = (Qsn_Win_Desc_T *) &storage;
    Q_Bin4 win_desc_length = sizeof(storage.win_desc) + strlen(wintitle);
    Qsn_Ssn_Desc_T *sess_desc = (Qsn_Ssn_Desc_T *) &ssnstorage;
    Q_Bin4          sess_desc_length = sizeof(Qsn_Ssn_Desc_T) + strlen(fkeys1);
    Qsn_Env_Desc_T  env_desc = {QSN_ENV_SAME, QSN_ENV_SAME, QSN_ENV_SAME,
                                QSN_ENV_SAME, QSN_COEXIST_NO, QSN_ENV_SAME, "*REQUESTER"
                               };
    char coldata [61];
    short i;

    QsnInzWinD(win_desc, win_desc_length, NULL);
    win_desc->GUI_support = '0';
    win_desc->top_row = 2;
    win_desc->left_col = 50;
    win_desc->num_rows = nbrColumns < 14 ? nbrColumns + 5 : 19;
    win_desc->num_cols = 50;
    win_desc->msg_line = '1';
    win_desc->title_offset = sizeof(Qsn_Win_Desc_T);
    win_desc->title_len = strlen(wintitle);
    memcpy(storage.buffer, wintitle, strlen(wintitle));
    QsnInzSsnD( sess_desc, sess_desc_length, NULL);
    sess_desc->scl_top_row = 2;
    sess_desc->scl_num_rows = 0; /* if num_records no fkeys displayed */
    sess_desc->scl_num_cols = 0;
    sess_desc->num_input_line_rows = 0;
    sess_desc->wrap = '0';
    sess_desc->cmd_key_desc_line_1_offset = sizeof(Qsn_Ssn_Desc_T);
    sess_desc->cmd_key_desc_line_1_len = strlen(fkeys1);
    memcpy(ssnstorage.buffer, fkeys1, strlen(fkeys1));

    QsnCrtSsn(sess_desc, sess_desc_length, NULL, 0, '0',
              win_desc, win_desc_length, &env_desc, 16, colsession, NULL);

    /* run through fptr-array and write to a DSM scroller                         */
    for (i = 0; i < nbrColumns; i++)
    {
        memset(coldata, ' ', sizeof(coldata));
        if (fptr[i].Digits > 0) /* display info about numerical fields              */
            snprintf(coldata, 60, "%4d %.10s %.20s %c %5d %d %d",
                     i, fptr[i].Field_Name,
                     fptr[i].Column_Heading,
                     fptr[i].Data_Type, fptr[i].Field_Length_Bytes,
                     fptr[i].Digits, fptr[i].Decimal_Positions);
        else
            snprintf(coldata, sizeof(coldata), "%4d %.10s %.20s %c %5d",
                     i, fptr[i].Field_Name,
                     fptr[i].Column_Heading,
                     fptr[i].Data_Type, fptr[i].Field_Length_Bytes);
        QsnWrtSclLin(*colsession, coldata, sizeof(coldata), NULL);
    }
}


static void setWindowArea(Qsn_Win_Desc_T  *win_desc,
                   int             toprow,
                   int             leftcol,
                   int             nbrrows,
                   int             nbrcols)
{
    win_desc->top_row = toprow;
    win_desc->left_col = leftcol;
    win_desc->num_rows = nbrrows;
    win_desc->num_cols = nbrcols;
}


static void displayWindow(Qsn_Win_T win)
{
    char aid;

    QsnStrWin(win, '1', NULL);
    for (;;)
    {
        if (( (aid = QsnGetAID(NULL, 0, NULL)) == QSN_F12))
        {
            QsnEndWin(win, '1', NULL);
            return;
        }
        else if (aid == QSN_F4)
        {
            QsnMovWinUsr(win, NULL);
        }
    }
}


static void displaySession(Qsn_Ssn_T ssn)
{
    char aid;

    QsnStrWin(ssn, '1', NULL);
    QsnDspWin(ssn, NULL);
    for (;;)
    {
        if (( (aid = QsnGetAID(NULL, 0, NULL)) == QSN_F12))
        {
            QsnEndWin(ssn, '1', NULL);
            return;
        }
        else if (aid == QSN_F4)
        {
            QsnMovWinUsr(ssn, NULL);
        }
        else if (aid == QSN_F7)
        {
            QsnShfSclL(ssn, 0, NULL);
        }
        else if (aid == QSN_F8)
        {
            QsnShfSclR(ssn, 0, NULL);
        }
        else if (aid == QSN_F11) /* should be cleaned up, not pretty in current state */
        {
            QsnTglSclWrp(ssn, NULL, NULL);
        }
        else if (aid == QSN_F17)
        {
            QsnDspSclT(ssn, NULL);
        }
        else if (aid == QSN_F18)
        {
            QsnDspSclB(ssn, NULL);
        }
        else if (aid == QSN_PAGEDOWN)
        {
            QsnRollSclUp(ssn, 0, NULL);
        }
        else if (aid == QSN_PAGEUP)
        {
            QsnRollSclDown(ssn, 0, NULL);
        }
    }
}


static void shiftView(int listw, int strlistcol, int dspreclen, unsigned long *rrn)
{
    int row = LISTSTRROW;
    short i;
    int k;
    char s [11];

    for (i = 0; i < listrrn; i++, row++)
    {
        if (strlistcol > LISTSTRCOL) 
        {
            snprintf(s, sizeof(s), "%10d", rrn[i]);
            QsnWrtDta(s, 10, 0, row, LISTSTRCOL, norm, norm, pink, norm, cmdbuf, envhandle, NULL);
        }
        k = handleRightmostView(row, listw, strlistcol, dspreclen);
        QsnWrtDta(listData[i] + wpos, k, 0, row, strlistcol, mcattr, mcattr,
                  blue, blue, cmdbuf, envhandle, NULL);
    }
}


static int handleRightmostView(int row, int listw, int strlistcol, int dspreclen)
{
    int len = dspreclen - wpos;
    if (len < listw)
    {
        /* remove pad... here and let each caller handle this */
        if (padWithSpaces(row, row, len + strlistcol, listw - len) == 1)
            ;
        return len;
    }
    else
    {
        return dspreclen < listw ? dspreclen : listw;
    }
}


static int padWithSpaces(int strrow, int endrow, int col, int len)
{
    int i = strrow;
    if (strrow > endrow)
    {
        return -1;
    }
    for (i; i <= endrow; i++)
    {
        QsnWrtPad(' ', len, 0, i, col, cmdbuf, envhandle, NULL);
    }
    return 1;
}


static int generateDDL (int arglen,
                 char *objname,
                 Qsn_Ssn_T *DDLsession,
                 const Qdb_Qdbfh_t *fddata,
                 const Qdb_Qdbflogl_t *lfdata)
{
    Qsq_SQLR0100_t *genddldata; 
    char cmd       [100],
         filename  [33],
         qtemplib  [10] = "QTEMP",
                          qtempfile [10] = "EODDLPF";
    char      *locbuf;
    _RFILE    *fp;
    _RIOFB_T  *fb;
    _XXOPFB_T *iofb;
    Qus_EC_t  *err;
    char       error_code [201];
    int        structlen;
    int        maxreclen = 0;
    static int DDL_generated = false;
    storage_t  storage;
    ssnstorage_t ssnstorage;
    char      *wintitle = "Generated SQL";
    char      *fkeys1 = "F4 Move F17 Top F18 Bottom";
    Qsn_Env_Desc_T  env_desc = {QSN_ENV_SAME, QSN_ENV_SAME, QSN_ENV_SAME,
                                QSN_ENV_SAME, QSN_COEXIST_NO, QSN_ENV_SAME, "*REQUESTER"
                               };
    Qsn_Win_Desc_T *win_desc = (Qsn_Win_Desc_T *) &storage;
    Q_Bin4 win_desc_length = sizeof(storage.win_desc) + strlen(wintitle);
    Qsn_Ssn_Desc_T  *sess_desc = (Qsn_Ssn_Desc_T *) &ssnstorage;
    Q_Bin4 sess_desc_length = sizeof(Qsn_Ssn_Desc_T) + strlen(fkeys1);


    if (DDL_generated == true)
    {
        return 1;
    }

    genddldata = (Qsq_SQLR0100_t *) malloc(sizeof(Qsq_SQLR0100_t));
    memset(genddldata, ' ', sizeof(Qsq_SQLR0100_t));
    memcpy(genddldata->Object_Name, objname, 10);
    memcpy(genddldata->Object_Library, objname + 10, 10);
    if (fddata->Qdbfhflg.Qdbfhfpl == 0)
    {
        memcpy(genddldata->Object_Type, "TABLE", 5);
    }
    else if (lfdata->Qlfa.Qdbfsqlv == 1)
    {
        memcpy(genddldata->Object_Type, "VIEW", 4);
    }
    else if (lfdata->Qlfa.Qdbfsqli == 1)
    {
        memcpy(genddldata->Object_Type, "INDEX", 5);
    }
    else
    {
        memcpy(genddldata->Object_Type, "VIEW", 4);
    }

    memcpy(genddldata->Source_File_Name, qtempfile, strlen(qtempfile));
    memcpy(genddldata->Source_File_Library, qtemplib, strlen(qtemplib));
    memcpy(genddldata->Source_File_Member, objname, arglen);
    genddldata->Severity_Level = 0;
    genddldata->Replace_Option = '1';
    genddldata->Statement_Formatting_Option = '0'; /* bad idea to have EOL in 5250 terminal */
    memcpy(genddldata->Date_Format, "ISO", 3);
    genddldata->Date_Separator = '-';
    memcpy(genddldata->Time_Format, "ISO", 3);
    genddldata->Time_Separator = ':';
    memcpy(genddldata->Naming_Option, "SYS", 3);
    genddldata->Decimal_Point = '.';
    genddldata->Standards_Option = '0';
    genddldata->Drop_Option = '0';
    genddldata->Message_Level = 0;
    genddldata->Comment_Option = '0';
    genddldata->Label_Option = '1';
    genddldata->Header_Option = '0';
    genddldata->Trigger_Option = '0'; /* below is not in the docs, been added later */
    genddldata->Constraint_Option = '0';
    genddldata->System_Name_Option = '0';

    /* create file holding the generated statement */
    snprintf(cmd, sizeof(cmd), "CRTSRCPF FILE(%s/%s) MBR(%.*s)", qtemplib, qtempfile, 
    	arglen, objname);
    if (system(cmd) != 0)
    {
        free(genddldata);
        return -1;
    }

    err = (Qus_EC_t *) &error_code[0];
    memset(err, 0, sizeof(*err));
    err->Bytes_Provided == sizeof(error_code) - 1;

    structlen = sizeof(Qsq_SQLR0100_t);  /* wrong datatype in documentation */
    QSQGNDDL(genddldata, &structlen, "SQLR0100", err);
    if (err->Bytes_Available > 0)
    {
        free(genddldata);
        snprintf(cmd, sizeof(cmd) - 1, "DLTF %s/%s", qtemplib, qtempfile);
        if (system(cmd) != 0)
            ; /* dang */
        return -1; /* ta hand om error eg */
    }

    snprintf(filename, sizeof(filename), "%s/%s (%.*s)", qtemplib, qtempfile, arglen, 
    	objname);
    if ((fp = _Ropen(filename, "rr")) == NULL)
    {
        free(genddldata);
        snprintf(cmd, sizeof(cmd) - 1, "DLTF %s/%s", qtemplib, qtempfile);
        if (system(cmd) != 0)
            ; /* dang */
        return -1;
    }

    iofb = _Ropnfbk(fp);

    /* create a DSM session            */
    QsnInzWinD(win_desc, win_desc_length, NULL);
    win_desc->GUI_support = '0';
    win_desc->top_row = 2;
    win_desc->left_col = 10;
    win_desc->num_rows = iofb->num_records < 18 ? iofb->num_records + 4 : 22;
    win_desc->msg_line = '1';
    win_desc->title_offset = sizeof(Qsn_Win_Desc_T);
    win_desc->title_len = strlen(wintitle);
    memcpy(storage.buffer, wintitle, strlen(wintitle));
    QsnInzSsnD( sess_desc, sess_desc_length, NULL);
    sess_desc->scl_top_row = 2;
    sess_desc->scl_num_rows = 0; /* if num_records then fkeys aren't visible */
    sess_desc->scl_num_cols = 0;
    sess_desc->num_input_line_rows = 0;
    sess_desc->wrap = '0';
    sess_desc->cmd_key_desc_line_1_offset = sizeof(Qsn_Ssn_Desc_T);
    sess_desc->cmd_key_desc_line_1_len = strlen(fkeys1);
    memcpy(ssnstorage.buffer, fkeys1, strlen(fkeys1));

    /* read file twice. once to see how wide the window needs to be to fit */
    locbuf = (char *) *fp->in_buf;
    fb = _Rreadf(fp, NULL, 0, __NO_LOCK);
    while (fb->num_bytes != EOF)
    {
        if (triml(locbuf, ' ') - 12 > maxreclen)
        {
            maxreclen = triml(locbuf, ' ') - 12;
        }
        fb = _Rreadn(fp, NULL, 0, __NO_LOCK);
    }
    win_desc->num_cols = maxreclen + 3;

    QsnCrtSsn(sess_desc, sess_desc_length, NULL, 0, '0',
              win_desc, win_desc_length, &env_desc, 16, DDLsession, NULL);

    fb = _Rreadf(fp, NULL, 0, __NO_LOCK);
    while (fb->num_bytes != EOF)
    {
        /* first 12 bytes is member junk, skip */
        QsnWrtSclLin(*DDLsession, locbuf + 12, maxreclen + 3, NULL);
        fb = _Rreadn(fp, NULL, 0, __NO_LOCK);
    }
    _Rclose(fp); 
    free(genddldata);
    snprintf(cmd, sizeof(cmd) - 1, "DLTF %s/%s", qtemplib, qtempfile);
    if (system(cmd) != 0)
        ; /* dang it */
    DDL_generated = true;
    return 1;
}


/* as already noted, handling of keys spanning several pages or lines not fixed yet  */
static int displayKeyWindow(_RFILE *fp, _RIOFB_T *fb, size_t keylen)
{
    short row = 6;
    short col = 10;
    inputData *pinput;
    Q_Fdbk_T dsmerr = {sizeof(Q_Fdbk_T)}; /* must be initialized or kaboom */
    int len;
    char charopts [2] = {"EQ"};
    int opts = __DFT | __NO_LOCK;
    char *keyvalue;
    char *msg = "Key not found. Try another or go back";

    setKeyWindowFields(&row, &col);  /* ought to store where keys are and their total size for coming malloc  */
    dsmerr.provided = sizeof(dsmerr); /* kaboom here if varchar and it's opened a second time */
    QsnStrWin(keywin, '1', &dsmerr);
    if (dsmerr.available > 0)
    {
        fprintf(stderr, "API QsnStrWin failed with error %s\n", dsmerr.exception_id);
        exit(EXIT_FAILURE);
    }

    QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0, cmdbuf, envhandle, NULL);
    QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
    pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    while (pinput->AID != QSN_F12)
    {
        QsnClrBuf(cmdbuf, NULL);
        if (pinput->AID == QSN_ENTER)
        {
            /* read keys, depending on how many are on display   */
            len = (QsnRtvDtaLen(inputbuf, NULL, NULL) - 3);
            if (len > 0)
            {
                short fieldpos = 0,
                      i = 0,
                      keypos = 0;
                char *tempptr;
                char *stopstring;
                union
                {
                    short shortval;
                    int   intval;
                    long long longlongval;
                } ints;

                keyvalue = malloc(keylen);
                memset(keyvalue, ' ', sizeof(keylen));
                if (memcmp(charopts, pinput->field + fieldpos, sizeof(charopts)) != 0)
                {
                    if (memcmp(pinput->field + fieldpos, "GE", sizeof(charopts)) != 0 &&
                            memcmp(pinput->field + fieldpos, "GT", sizeof(charopts)) != 0 &&
                            memcmp(pinput->field + fieldpos, "LE", sizeof(charopts)) != 0 &&
                            memcmp(pinput->field + fieldpos, "LT", sizeof(charopts)) != 0)
                        /* invalid options so let's use EQ (equals) */
                        ;
                    else
                    {
                        if (memcmp(pinput->field + fieldpos, "GE", sizeof(charopts)) == 0)
                        {
                            opts = __KEY_GE | __NO_LOCK;
                        }
                        else if (memcmp(pinput->field + fieldpos, "GT", sizeof(charopts)) == 0)
                        {
                            opts = __KEY_GT | __NO_LOCK;
                        }
                        else if (memcmp(pinput->field + fieldpos, "LE", sizeof(charopts)) == 0)
                        {
                            opts = __KEY_LE | __NO_LOCK;
                        }
                        else
                        {
                            opts = __KEY_LT | __NO_LOCK;
                        }

                    }
                }
                fieldpos += sizeof(charopts);

                for (i = 0; i < keydata->Rec_Key_Info->Num_Of_Keys; i++)
                {
                    void *decptr;
                    char *s;
                    short slen;
                    /* partial keys, all other fields are blank */
                    char *blankkey;

                    /* should change to MI function _CMPTOPAD */
                    blankkey = malloc(len - fieldpos);
                    memset(blankkey, ' ', len - fieldpos);
                    if (memcmp(pinput->field + fieldpos, blankkey, len - fieldpos) == 0)
                    {
                        free(blankkey);
                        break;
                    }
                    free(blankkey);

                    tempptr = malloc(keyfldinfo[i].dsplen + 1);
                    memset(tempptr, ' ', keyfldinfo[i].dsplen);
                    tempptr[keyfldinfo[i].dsplen] = '\0';
                    memcpy(tempptr, pinput->field + fieldpos, keyfldinfo[i].dsplen);
                    switch (keyfldinfo[i].Data_Type)
                    {
                        case 0x0000: /*binary */
                            if (keyfldinfo[i].Field_Len == 2)
                            {
                                ints.shortval = atoi(tempptr);
                                memcpy(keyvalue + keypos, (const void *) &ints.shortval, 
                                	sizeof(short));
                                keypos += sizeof(short);
                            }
                            else if (keyfldinfo[i].Field_Len == 4)
                            {
                                ints.intval = atoi(tempptr);
                                memcpy(keyvalue + keypos, (const void *) &ints.intval, 
                                	sizeof(int));
                                keypos += sizeof(int);
                            }
                            else if (keyfldinfo[i].Field_Len == 8)
                                /* must be tested... */
                            {
                                ints.longlongval = strtoll(tempptr, &stopstring, 0);
                                memcpy(keyvalue + keypos, (const void *) &ints.longlongval, 
                                	sizeof(long long));
                                keypos += sizeof(long long);
                            }
                            break;

                        case 0x0001: /* float */
                            break;

                        case 0x0003: /* packed dec */
                            decptr = malloc(keyfldinfo[i].Field_Len);
                            memset(decptr, ' ', keyfldinfo[i].Field_Len); /* test avoid crash */
                            s = malloc(keyfldinfo[i].dsplen);
                            slen = triml(tempptr, ' ');
                            memset(s, '0', keyfldinfo[i].dsplen);
                            /* right align inputted data        */
                            memcpy(s + (keyfldinfo[i].dsplen - slen), tempptr, slen);
                            cpynv(NUM_DESCR(_T_PACKED, 
                            	keyfldinfo[i].Num_Of_Digs, keyfldinfo[i].Dec_Pos), decptr,
                                NUM_DESCR(_T_ZONED, keyfldinfo[i].Num_Of_Digs, 
                                keyfldinfo[i].Dec_Pos), s);
                            memcpy(keyvalue + keypos, (const void *) decptr, 
                            	keyfldinfo[i].Field_Len);
                            keypos += keyfldinfo[i].Field_Len;
                            free(decptr);
                            free(s);
                            break;

                        case 0x8004: /* varchar */
                            ints.shortval = triml(tempptr, ' ');
                            memcpy(keyvalue + keypos, (const void *) &ints.shortval, 
                            	sizeof(short));
                            keypos += sizeof(short);
                            memcpy(keyvalue + keypos, tempptr, keyfldinfo[i].dsplen); 
                            keypos += keyfldinfo[i].Field_Len;
                            break;

                            /* alfa, zoned etc */
                        case 0x0002: /* zoned */
                        case 0x0004: /* char */
                        case 0x000B: /* date */
                        case 0x000C: /* time */
                        case 0x000D: /* timestamp */
                            memcpy(keyvalue + keypos, tempptr, keyfldinfo[i].Field_Len);
                            keypos += keyfldinfo[i].Field_Len;
                            break;
                            /* remaining datatypes should go here... */

                    }
                    fieldpos += keyfldinfo[i].dsplen;
                    free(tempptr);
                }

                fb = _Rlocate(fp, keyvalue, keypos, opts);
                if (fb->num_bytes == 1)
                {
                    _Rlocate(fp, NULL, 0, __PREVIOUS);
                    row = 6;
                    col = 10;
                    QsnEndWin(keywin, '1', NULL);
                    QsnClrBuf(cmdbuf, NULL);
                    free(keyvalue);
                    return 1; /* successful repositioning */
                }
                else
                {
                    free(keyvalue);
                    QsnWrtDta(msg, strlen(msg), 0, 24, 2, norm, norm, white, norm, cmdbuf, 
                    	keywin, NULL);
                    opts = __DFT | __NO_LOCK;
                    QsnSetCsrAdr(0, 6, col + 11, cmdbuf, keywin, NULL);
                    QsnWrtDta(charopts, sizeof(charopts), 0, 4, 22, norm, norm, 
                    	QSN_SA_TRQ_UL, norm, cmdbuf, keywin, NULL);
                }
            }
        }
        QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
                   cmdbuf, envhandle, NULL);
        QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
        pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    }
    row = 6;
    col = 10;
    QsnEndWin(keywin, '1', NULL);
    QsnClrBuf(cmdbuf, NULL);
    /*free(keyvalue);  */
    return 0;
}


static void setKeyWindowFields(short *row, short *col)
{
    char *keywinText [] = {"  Position by Key  ",
                           "Enter=Reposition F12=Previous",
                           "Key option:",
                           "(EQ, LE, LT, GE, GT)"
                          };
    short i;
    unsigned short ffw;
    char opts [2] = {"EQ"};

    /* only for debug of the infamous varchar situation, i.e. 2nd varchar crashes */
    static int why = 0;
    why++;    /* break 1368 when why>1 */

    QsnWrtDta(keywinText[0], strlen(keywinText[0]), 0, 2, 50, norm, norm, QSN_SA_WHT_RI, 
    	norm, cmdbuf, keywin, NULL);

    /* a field with default value EQ allowing values of LE, GT etc */
    QsnWrtDta(keywinText[2], strlen(keywinText[2]), 0, *(row) - 2, *col, norm, norm, pink, 
    	norm, cmdbuf, keywin, NULL);
    QsnSetFld(0, sizeof(opts), *(row) - 2, *(col) + strlen(keywinText[2]) + 1, 
    	QSN_FFW_AUTO_MONOCASE, NULL, 0, QSN_SA_UL, QSN_SA_TRQ_UL, cmdbuf, keywin, NULL);
    QsnWrtDta(opts, sizeof(opts), 0, *(row) - 2, *(col) + strlen(keywinText[2]) + 1, norm, 
    	norm, QSN_SA_TRQ_UL, norm, cmdbuf, keywin, NULL);
    QsnWrtDta(keywinText[3], strlen(keywinText[3]), 0, *(row) - 2, 26, norm, norm, pink, norm,
              cmdbuf, keywin, NULL);


    for (i = 0; i < keydata->Rec_Key_Info->Num_Of_Keys && *row < 19; i++)
    {
        QsnWrtDta(keyfldinfo[i].keyname, sizeof(keyfldinfo[i].keyname), 0, *row, *col,
                  norm, norm, blue, norm, cmdbuf, keywin, NULL);

        switch (keyfldinfo[i].Data_Type)
        {
            case 0x0000:  /* binary */
                ffw = QSN_FFW_DIGIT_ONLY;
                keyfldinfo[i].dsplen = keyfldinfo[i].Num_Of_Digs;
                break;

            case 0x0001: /* float */
                /* need to fix how to input data into a float field */
                break;

            case 0x0002: /* zoned */
                ffw = QSN_FFW_NUM_ONLY;
                keyfldinfo[i].dsplen = keyfldinfo[i].Field_Len;
                break;

            case 0x0003: /* packed */
                ffw = QSN_FFW_NUM_ONLY;
                if (keyfldinfo[i].Dec_Pos == 0)
                {
                    keyfldinfo[i].dsplen = keyfldinfo[i].Num_Of_Digs;
                }
                else
                {
                    keyfldinfo[i].dsplen = keyfldinfo[i].Num_Of_Digs + 1;    
                }
                break;

            case 0x0004: /* alpha    */
            case 0x8004: /* varchar  */
                ffw = QSN_FFW_ALPHA_SHIFT;
                if (keyfldinfo[i].Data_Type == 0x0004)
                {
                    keyfldinfo[i].dsplen = keyfldinfo[i].Field_Len;
                }
                else
                {
                    keyfldinfo[i].dsplen = keyfldinfo[i].Field_Len - 2;
                }
                break;
                /* dbcs also exists on the fcw-parmen if we should handle that too some time*/

            case 0x000B: /* date */
            case 0x000D: /* timestamp */
                ffw = QSN_FFW_MF; /* mandatory filled, i.e. all 10 pos */
                keyfldinfo[i].dsplen = keyfldinfo[i].Field_Len;
                break;
                /* more to follow, "time" for instance */
        }
        QsnSetFld(0, keyfldinfo[i].dsplen, *row, *(col) + 11, ffw, NULL, 0, QSN_SA_UL, 
        	QSN_SA_RED_UL, cmdbuf, keywin, NULL);
        (*row)++;
    }
    QsnSetCsrAdr(0, 6, *(col) + 11, cmdbuf, keywin, NULL);
    QsnWrtDta(keywinText[1], strlen(keywinText[1]), 0, 25, 2, norm, norm, blue, norm,
              cmdbuf, keywin, NULL);
    return;
}


static void dspSingleRow(int csrrow, int max, int nbrColumns, char *mbrname)
{
    short row = 7;
    short col = 10;
    char aid;
    int fldnbr = 0,
        fldpos = 0,
        firstpage = true,
        savefldnbr = 0,
        savefldpos = 0;
    static int colview = true;

    setSingleRowFlds(csrrow - LISTSTRROW, &row, &col, &fldnbr, &fldpos, mbrname, max, 
    	nbrColumns, colview);
    QsnStrWin(sngrowwin, '1', NULL);
    QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
               cmdbuf, envhandle, NULL);
    QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
    pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    while (pinput->AID != QSN_F12)
    {
        QsnClrBuf(cmdbuf, NULL);
        if (pinput->AID == QSN_PAGEDOWN && fldnbr < nbrColumns)
        {
            firstpage = false;
            row = 7;
            col = 10;
            QsnClrScr('0', cmdbuf, envhandle, NULL);
            savefldnbr = fldnbr;
            savefldpos = fldpos;
            setSingleRowFlds(csrrow - LISTSTRROW, &row, &col, &fldnbr, &fldpos, mbrname, 
            	max, nbrColumns, colview);
        }
        else if (pinput->AID == QSN_PAGEUP && firstpage == false)
        {
            pageUpSingleRow(&row, &fldnbr, &fldpos, nbrColumns);
            if (fldnbr == 0 && fldpos == 0)
            {
                firstpage = true;
            }
            row = 7;
            QsnClrScr('0', cmdbuf, envhandle, NULL);
            savefldnbr = fldnbr;
            savefldpos = fldpos;
            setSingleRowFlds(csrrow - LISTSTRROW, &row, &col, &fldnbr, &fldpos, mbrname, 
            	max, nbrColumns, colview);
        }
        else if (pinput->AID == QSN_F8) /* switch between colnames and data types */
        {
            if (colview == true)
            {
                colview = false;
            }
            else
            {
                colview = true;
            }        
            row = 7;
            QsnClrScr('0', cmdbuf, envhandle, NULL);
            fldnbr = savefldnbr;
            fldpos = savefldpos;
            setSingleRowFlds(csrrow - LISTSTRROW, &row, &col, &fldnbr, &fldpos, mbrname, max, nbrColumns, colview);

        }
        QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
                   cmdbuf, envhandle, NULL);
        QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
        pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    }
    row = 7;
    col = 10;
    QsnEndWin(sngrowwin, '1', NULL);
    QsnClrBuf(cmdbuf, NULL);
    return;
}


static void setSingleRowFlds(int listrow, short *row, short *col, int *fldnbr, int *fldpos, 
	char *mbrname, int maxfldsize, int nbrColumns, int colview)
{
    char *sngRowWinText [] = {"  Single Row  ",
                              "F8 Column/Datatypes view F12=Previous",
                              "File:           ",
                              "Lib:            ",
                              "Mbr:            ",
                              "Column      Seq  Value",
                              "Column Heading",
                              "More lines exist",
                              "No more lines",
                              "Datatypes          Seq  Value"
                             };
    unsigned short ffw = QSN_FFW_BYPASS;
    int nbrFldLines,
        idx = 0,
        headlinerow = 2,
        fldstrpos = 17,
        rowlen = maxfldsize > MAXFLDROWLEN ? MAXFLDROWLEN : maxfldsize;
    char fldseq [4];

    /* some headlines etc */
    QsnWrtDta(sngRowWinText[0], strlen(sngRowWinText[0]), 0, 2, 55, norm, norm, QSN_SA_WHT_RI, norm,
              cmdbuf, sngrowwin, NULL);

    snprintf(sngRowWinText[2] + 6, strlen(sngRowWinText[2]), "%.10s", field_hdr->File_Name_Used);
    QsnWrtDta(sngRowWinText[2], strlen(sngRowWinText[2]), 0, headlinerow, 2, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);
    snprintf(sngRowWinText[3] + 6, strlen(sngRowWinText[3]), "%.10s", field_hdr->Library_Name);
    QsnWrtDta(sngRowWinText[3], strlen(sngRowWinText[3]), 0, headlinerow + 1, 2, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);
    snprintf(sngRowWinText[4] + 6, strlen(sngRowWinText[4]), "%.10s", mbrname);
    QsnWrtDta(sngRowWinText[4], strlen(sngRowWinText[4]), 0, headlinerow + 2, 2, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);
    if (colview == true)
    {
        QsnWrtDta(sngRowWinText[5], strlen(sngRowWinText[5]), 0, *(row) - 1, *col, mcattr, mcattr,
                  white, white, cmdbuf, envhandle, NULL);
    }
    else
    {
        QsnWrtDta(sngRowWinText[9], strlen(sngRowWinText[9]), 0, *(row) - 1, *(col) - 7, mcattr, mcattr,
                  white, white, cmdbuf, envhandle, NULL);
    }
    QsnWrtDta(sngRowWinText[6], strlen(sngRowWinText[6]), 0, *(row) - 1, *(col) + rowlen + fldstrpos + 3, mcattr, mcattr,
              white, white, cmdbuf, envhandle, NULL);

    for (*fldnbr; *fldnbr < nbrColumns && *row < 22; )
    {
        if (colview == true)
        {
            QsnWrtDta(fptr[*fldnbr].Field_Name, sizeof(fptr[*fldnbr].Field_Name), 0, *row, 
            	*(col) - 1, /* -1 needed, strange */
                norm, norm, blue, norm, cmdbuf, sngrowwin, NULL);
        }
        else
        {
            QsnWrtDta(fptr[*fldnbr].dataTypeString, sizeof(fptr[*fldnbr].dataTypeString), 0, *row, *(col) - 8,
                      norm, norm, blue, norm, cmdbuf, sngrowwin, NULL);
        }
        memset(fldseq, ' ', sizeof(fldseq));
        snprintf(fldseq, sizeof(fldseq), "%d", *fldnbr);
        QsnWrtDta(fldseq, sizeof(fldseq), 0, *row, *(col) + 11, norm, norm, pink, norm, 
        	cmdbuf, sngrowwin, NULL);
        nbrFldLines = 0;

        /* handle fields broken over several rows  */
        if (fptr[*fldnbr].fldsize > MAXFLDROWLEN)
        {
            nbrFldLines = (fptr[*fldnbr].fldsize - *fldpos) / MAXFLDROWLEN;
        }

        if (nbrFldLines > 0)
        {
            int j = 0,
                contFldEntry = true;
            const unsigned short first = QSN_FCW_CONT_FIRST,
                                 middle = QSN_FCW_CONT_MIDDLE,
                                 last = QSN_FCW_CONT_LAST;

            QsnWrtDta(fptr[*fldnbr].Column_Heading, 40, 0, *row, *(col) + rowlen + fldstrpos + 2,
                      norm, norm, blue, norm, cmdbuf, sngrowwin, NULL);
            /* check if whole field fits on screen, if so "continued field entry" */
            if (fptr[*fldnbr].fldsize > ((15 - (*(row) - 7)) * MAXFLDROWLEN))
            {
                contFldEntry = false;
            }

            for (j = 0; j < nbrFldLines && *row < 22; j++, (*row)++)
            {
                if (contFldEntry == true)
                {
                    if (j == 0)
                    {
                        QsnSetFld(0, MAXFLDROWLEN, *row, *(col) + fldstrpos, ffw, &first,
                                  1, QSN_SA_CS, QSN_SA_TRQ_CS, cmdbuf, sngrowwin, NULL);
                    }
                    else if (j == nbrFldLines - 1 && fptr[*fldnbr].fldsize % MAXFLDROWLEN == 0)
                    {
                        QsnSetFld(0, MAXFLDROWLEN, *row, *(col) + fldstrpos, ffw, &last,
                                  1, QSN_SA_CS, QSN_SA_TRQ_CS, cmdbuf, sngrowwin, NULL);
                    }              
                    else
                    {
                        QsnSetFld(0, MAXFLDROWLEN, *row, *(col) + fldstrpos, ffw, &middle,
                                  1, QSN_SA_CS, QSN_SA_TRQ_CS, cmdbuf, sngrowwin, NULL);
                    }
                else
                {
                    QsnSetFld(0, MAXFLDROWLEN, *row, *(col) + fldstrpos, ffw, NULL,
                              0, QSN_SA_CS, QSN_SA_TRQ_CS, cmdbuf, sngrowwin, NULL);
                }
                QsnWrtDta(listData[listrow] + fptr[*fldnbr].strpos + *fldpos, MAXFLDROWLEN, 
                	0, *row, *(col) + fldstrpos, norm, norm, QSN_SA_TRQ_CS, norm, cmdbuf, 
                	envhandle, NULL);
                *fldpos += MAXFLDROWLEN;
                idx++;
            }
            /* field ends in current screen */
            if (fptr[*fldnbr].fldsize - *fldpos <= MAXFLDROWLEN && *row <= 21) 
            {
                /* handle last < 50 row if it exists */
                if (fptr[*fldnbr].fldsize % MAXFLDROWLEN != 0)
                {
                    if (contFldEntry == true)
                    {
                        QsnSetFld(0, fptr[*fldnbr].fldsize % MAXFLDROWLEN, *row, 
                        	*(col) + fldstrpos, ffw, &last, 1, QSN_SA_CS, QSN_SA_TRQ_CS, 
                        	cmdbuf, sngrowwin, NULL);
                    }
                    else
                    {
                        QsnSetFld(0, fptr[*fldnbr].fldsize % MAXFLDROWLEN, *row, 
                        	*(col) + fldstrpos, ffw, NULL, 0, QSN_SA_CS, QSN_SA_TRQ_CS, 
                        	cmdbuf, sngrowwin, NULL);
                    }
                    QsnWrtDta(listData[listrow] + fptr[*fldnbr].strpos + *fldpos, fptr[*fldnbr].fldsize % MAXFLDROWLEN, 0, *row, *(col) + fldstrpos,
                              norm, norm, QSN_SA_TRQ_CS, norm, cmdbuf, envhandle, NULL);
                    idx++;
                    (*row)++;
                }
                (*fldnbr)++;
                *fldpos = 0;
            }
            else if (*fldpos == fptr[*fldnbr].fldsize)
            {
                (*fldnbr)++;
                *fldpos = 0;
            }

        }
        else /* length <= 50 */
        {
            QsnSetFld(0, fptr[*fldnbr].fldsize - *fldpos, *row, *(col) + fldstrpos, ffw, 
            	NULL, 0, QSN_SA_CS, QSN_SA_TRQ_CS, cmdbuf, sngrowwin, NULL);
            QsnWrtDta(listData[listrow] + fptr[*fldnbr].strpos + *fldpos, 
            	fptr[*fldnbr].fldsize - *fldpos, 0, *row, *(col) + fldstrpos, norm, norm,
                QSN_SA_TRQ_CS, norm, cmdbuf, envhandle, NULL);
            QsnWrtDta(fptr[*fldnbr].Column_Heading, 40, 0, *row, *(col) + rowlen + 
            	fldstrpos + 2, norm, norm, blue, norm, cmdbuf, sngrowwin, NULL);
            idx++;
            (*row)++;
            (*fldnbr)++;
            *fldpos = 0;
        }
    }
    if (*fldnbr < nbrColumns)
    {
        QsnWrtDta(sngRowWinText[7], strlen(sngRowWinText[7]), 0, 24, 80, norm, norm, pink, 
        	norm, cmdbuf, sngrowwin, NULL);
    }
    else
    {
        QsnWrtDta(sngRowWinText[8], strlen(sngRowWinText[8]), 0, 24, 80, norm, norm, pink, 
        	norm, cmdbuf, sngrowwin, NULL);
    }

    QsnWrtDta(sngRowWinText[1], strlen(sngRowWinText[1]), 0, 25, 2, norm, norm, blue, norm,
              cmdbuf, sngrowwin, NULL);
    return;
}


static void pageUpSingleRow(short *row, int *fldnbr, int *fldpos, int nbrColumns)
{
    int linesToGoBack = *row - 7 + 15;

    if (*fldnbr > nbrColumns - 1)
    {
        (*fldnbr)--;
        if (fptr[*fldnbr].fldsize > MAXFLDROWLEN)
        {
            *fldpos = fptr[*fldnbr].fldsize;
        }
        else
        {
            linesToGoBack--;
        }
    }

    for (linesToGoBack; linesToGoBack > 0; )
    {
        /* scenario that fldpos is > 0 means that a field > 50 is on screen  */
        if (*fldpos > 0)
        {
            int fldrem = *fldpos % MAXFLDROWLEN; /* not ideal that is done every time */

            if (fldrem != 0)
            {
                *fldpos -= fldrem;
            }
            else
            {
                *fldpos -= MAXFLDROWLEN;
            }

            if (*fldpos == 0 && linesToGoBack > 1)
            {
                (*fldnbr)--;
                if (fptr[*fldnbr].fldsize > MAXFLDROWLEN)
                {
                    *fldpos = fptr[*fldnbr].fldsize;
                }
                else
                {
                    linesToGoBack--;
                }
            }
            linesToGoBack--;
        }
        else
        {
            (*fldnbr)--;
            if (fptr[*fldnbr].fldsize > MAXFLDROWLEN)
            {
                *fldpos = fptr[*fldnbr].fldsize;
            }
            else
            {
                linesToGoBack--;
            }
        }
    }
}


static int createMemberList (char *filename, char *usname, mbrnames **ppmbrnames)
{
    Qus_EC_t userror;
    int i = 0;
    mbrnames *p;
    Qdb_Ldbm_Header_t   *field_hdr;
    Qdb_Ldbm_MBRL0200_t *field_list;
    char *hdr_section,
         *list_section;

    userror.Bytes_Provided = 0;
    QUSLMBR(usname, "MBRL0200", filename, "*ALL      ", "0", &userror);

    userror.Bytes_Provided = 0;
    QUSPTRUS(usname, &space, &userror);

    char_space = (char *)space;   // lever kvar sen f�rr annars?
    list_section = char_space;
    hdr_section = list_section + space->Offset_Header_Section;
    field_hdr = (Qdb_Ldbm_Header_t *) hdr_section;
    list_section = list_section + space->Offset_List_Data;
    field_list = (Qdb_Ldbm_MBRL0200_t *) list_section;

    p = malloc(sizeof(mbrnames) * space->Number_List_Entries);
    /* skriva till ett array, sen f�r vi se till att ladda f�nstret med dess inneh�ll*/
    for (i; i < space->Number_List_Entries; i++)
    {
        memcpy(p[i].membername, field_list->Member_Name, sizeof(field_list->Member_Name));
        memcpy(p[i].mbrtxt, field_list->Member_Description, sizeof(field_list->Member_Description));
        field_list = (Qdb_Ldbm_MBRL0200_t *) ((char *) field_list + space->Size_Each_Entry);
    }
    *ppmbrnames = p;
    return 1;
}


/*
 * if 1 is returned a new member has been selected
 */
static int displayMemberWindow (Qsn_Win_T *mbrwin,
                         mbrnames *pmbrnames,
                         char *selectedMember,
                         char *filename,
                         short nbrMbrs,
                         int  jobCCSID)
{
    char *winMbrText [] = {"  File Member List  ",
                           "F9 Select via cursor F16 Regex filter F17 Top F18 Bottom F12=Previous",
                           "File:           ",
                           "Lib:            ",
                           "Nbr   Member     Description",
                           " ",
                           "Regex Member",
                           "Cursor not in the list. Try again",
                           "Regex Desc  "
                          };


    Qsn_Win_Desc_T win_desc;
    Q_Bin4         win_desc_length = sizeof(win_desc);
    int strrow = MBRLIST_STRROW;
    int strcol = 5;
    int idx = 0;
    int currow;
    int lastpage = false;
    int firstpage = true;
    int rem;
    int fldlen = 30;
    char aid;
    char temp[20];
    /* inputData *pinput; */
    mbrnames *regexsubset;
    short nbrmatches;
    int regexOn = false;
    int reload = false;
    const Q_Uchar cc1 = QSN_CC1_MDTALL_CLRALL,
                  cc2 = QSN_CC2_UNLOCKBD;
    QsnInzWinD(&win_desc, win_desc_length, NULL);
    win_desc.fullscreen = '1';
    win_desc.GUI_support = '0';
    *mbrwin = QsnCrtWin(&win_desc, win_desc_length, NULL, 0, '0', NULL, 0, NULL, NULL);

    /* create headlines here, not in load list function */
    QsnWrtDta(winMbrText[0], strlen(winMbrText[0]), 0, 2, 55, norm, norm, QSN_SA_TRQ_UL_RI, 
    	norm, cmdbuf, *mbrwin, NULL);
    QsnWrtDta(winMbrText[4], strlen(winMbrText[4]), 0, strrow - 1, strcol, norm, norm,
              white, white, cmdbuf, *mbrwin, NULL);
    snprintf(winMbrText[2] + 6, sizeof(winMbrText[2]), "%.10s", filename);
    QsnWrtDta(winMbrText[2], strlen(winMbrText[2]), 0, 3, strcol, norm, norm,
              white, white, cmdbuf, *mbrwin, NULL);
    snprintf(winMbrText[3] + 6, sizeof(winMbrText[3]), "%.10s", filename + 10);
    QsnWrtDta(winMbrText[3], strlen(winMbrText[3]), 0, 4, strcol, norm, norm,
              white, white, cmdbuf, *mbrwin, NULL);
    /* Antal mbrs */
    snprintf(temp, sizeof(temp), "%s %d", "Nbr Members:", nbrMbrs);
    QsnWrtDta(temp, strlen(temp), 0, 2, strcol, norm, norm,
              white, white, cmdbuf, *mbrwin, NULL);
    /* fkeys */
    QsnWrtDta(winMbrText[1], strlen(winMbrText[1]), 0, 26, strcol, norm, norm,
              blue, blue, cmdbuf, *mbrwin, NULL);

    /* fields for regex */
    QsnWrtDta(winMbrText[6], strlen(winMbrText[6]), 0, 4, 22, norm, norm,
              red, norm, cmdbuf, *mbrwin, NULL);
    QsnSetFld(0, fldlen, 4, 37, QSN_FFW_NUM_SHIFT, NULL, 0,
              QSN_SA_UL, QSN_SA_WHT_UL, cmdbuf, *mbrwin, NULL);
    QsnWrtDta(winMbrText[8], strlen(winMbrText[8]), 0, 5, 22, norm, norm,
              red, norm, cmdbuf, *mbrwin, NULL);
    QsnSetFld(0, fldlen, 5, 37, QSN_FFW_NUM_SHIFT, NULL, 0,
              QSN_SA_UL, QSN_SA_WHT_UL, cmdbuf, *mbrwin, NULL);

    loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, &currow);
    QsnStrWin(*mbrwin, '1', NULL);
    QsnWTD(cc1, cc2, cmdbuf, 0, NULL);
    while (true)
    {
        Q_Bin4 csrrow = 0,
               numflds;

        numflds = QsnReadMDT(QSN_CC1_NULL, QSN_CC1_NULL, NULL, inputbuf, cmdbuf, 0, NULL);
        QsnClrBuf(cmdbuf, NULL);
        QsnClrWinMsg(*mbrwin, NULL);
        QsnGetCsrAdr(&csrrow, NULL, envhandle, NULL);

        if (( (aid = QsnRtvReadAID(inputbuf, NULL, NULL)) == QSN_F12))
        {
            break;
        }
        if (aid == QSN_F9)
        {
            if (csrrow >= MBRLIST_STRROW && csrrow < MBRLIST_STRROW + (currow < LISTSIZE ? 
            	currow : LISTSIZE))
            {
                int selectedidx = -1;
                /* get idx for record: eg idx 59 row 15, csrrow 22 (-7 == 15). 
                   Top idx = 60 - 15 = 45 */
                if (currow == LISTSIZE && csrrow >= MBRLIST_STRROW && csrrow 
                	< MBRLIST_STRROW + LISTSIZE)
                {
                    selectedidx = (idx - LISTSIZE) + csrrow - MBRLIST_STRROW;
                }
                else if (currow < LISTSIZE && csrrow >= MBRLIST_STRROW && csrrow 
                	< MBRLIST_STRROW + currow)
                {
                    selectedidx = (idx - currow) + (csrrow - MBRLIST_STRROW);
                }

                if (selectedidx < nbrMbrs && regexOn == false)
                {
                    memcpy(selectedMember, pmbrnames[selectedidx].membername, 
                    	sizeof(pmbrnames->membername));
                    QsnEndWin(*mbrwin, '1', NULL);
                    QsnClrBuf(cmdbuf, NULL);
                    return 1;
                }
                else if (selectedidx < nbrmatches && regexOn == true)
                {
                    memcpy(selectedMember, regexsubset[selectedidx].membername, 
                    	sizeof(pmbrnames->membername));
                    free(regexsubset);
                    QsnEndWin(*mbrwin, '1', NULL);
                    QsnClrBuf(cmdbuf, NULL);
                    return 1;
                }
            }
            else
                QsnPutWinMsg(*mbrwin, winMbrText[7], strlen(winMbrText[7]), '0', NULL, 
                	NULL, 0, 0, norm, norm, red, norm, NULL);

        }
        else if (aid == QSN_PAGEDOWN && lastpage == false)
        {
            firstpage = false;
            if ( (regexOn == false && idx == nbrMbrs) ||
                    (regexOn == true && idx == nbrmatches) )
            {
                lastpage = true;
            }
            else
            {
                if (regexOn == false)
                {
                    loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, 
                    	&currow);
                }
                else
                {
                    loadMemberList(*mbrwin, regexsubset, nbrmatches, &idx, strrow, strcol, 
                    	&currow);
                }
            }
        }
        else if (aid == QSN_PAGEUP && firstpage == false)
        {
            if (currow - LISTSIZE < 0)
            {
                idx -= (idx % LISTSIZE) + LISTSIZE;
            }
            else
            {
                idx -= (LISTSIZE * 2);
            }

            lastpage = false;
            if (idx <= 0)
            {
                firstpage = true;
                idx = 0;
            }
            if (regexOn == false)
            {
                loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, &currow);
            }
            else
            {
                loadMemberList(*mbrwin, regexsubset, nbrmatches, &idx, strrow, strcol, 
                	&currow);
            }
        }
        else if (aid == QSN_F17 && firstpage == false)
        {
            idx = 0;
            firstpage = true;
            lastpage = false;
            if (regexOn == false)
            {
                loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, &currow);
            }
            else
            {
                loadMemberList(*mbrwin, regexsubset, nbrmatches, &idx, strrow, strcol, 
                	&currow);
            }
        }
        else if (aid == QSN_F18)
        {
            if (regexOn == false && nbrMbrs >= LISTSIZE)
            {
                idx = nbrMbrs - LISTSIZE;
                lastpage = true;
                firstpage = false;
                loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, &currow);
            }
            else if (regexOn == true && nbrmatches >= LISTSIZE)
            {
                idx = nbrmatches - LISTSIZE;
                lastpage = true;
                firstpage = false;
                loadMemberList(*mbrwin, regexsubset, nbrmatches, &idx, strrow, strcol, 
                	&currow);
            }
        }
        else if (aid == QSN_F16)
        {
            fldinfo_t  fldinfobuf;
            Qsn_Fld_Inf_T *fldqry = (Qsn_Fld_Inf_T *) &fldinfobuf;
            nbrmatches = 0;
            /* should not have to do this if locale was set on the box (per user?) */
            setlocale(LC_CTYPE, "/QSYS.LIB/SV_SE.LOCALE");

            if (numflds > 1)
            {
                char errmsg[70];
                snprintf(errmsg, sizeof(errmsg), "%s", "Only one regex field can be used at a time.");
                QsnPutWinMsg(*mbrwin, errmsg, strlen(errmsg), '0', NULL, NULL, 0, 0, norm, 
                	norm, red, norm, NULL);
                continue;
            }
            else if (numflds > 0)
            {
                regex_t    preg;
                int        rc;
                size_t     nmatch = 1;
                regmatch_t pmatch[1];
                char       pattern[31];

                /* currently hardcoded, nice */
                setlocale(LC_CTYPE, "/QSYS.LIB/SV_SE.LOCALE");

                QsnRtvFldInf(inputbuf, numflds, fldqry, sizeof(*fldqry), 0, NULL); 
                memset(pattern, ' ', sizeof(pattern)); 
                memcpy(pattern, fldqry->data, fldqry->len);
                pattern[fldqry->len] = '\0';

                if ((rc = regcomp(&preg, pattern, REG_EXTENDED)) != 0)
                {
                    char regerrbuf [100];
                    char convregerrbuf [100];
                    char errmsg [200];
                    char       *inbuf;
                    char       *outbuf;
                    iconv_t    cd;
                    struct QtqCode fromCode = {37, 0, 0, 0, 0, 0};
                    struct QtqCode toCode =   {0, 0, 0, 0, 0, 0}; /* uses CCSID of job  */
                    size_t ilen, olen, length;

                    toCode.CCSID = jobCCSID;
                    regerror(rc, &preg, regerrbuf, sizeof(regerrbuf));
                    cd = QtqIconvOpen(&toCode, &fromCode);
                    if (cd.return_value == -1)
                        ;
                    length = olen = ilen = strlen(regerrbuf);
                    inbuf = &regerrbuf[0];
                    outbuf = &convregerrbuf[0];
                    errno = 0;
                    rc = iconv(cd, &inbuf, &ilen, &outbuf, &olen);
                    /*borde eg skriv err */
                    convregerrbuf[length] = '\0'; /* after iconv ilen and olen are 0, and the end of the string can contain junk */
                    snprintf(errmsg, sizeof(errmsg), "regex '%s' failed: %s", 
                    	pattern, convregerrbuf);
                    QsnPutWinMsg(*mbrwin, errmsg, strlen(errmsg), '0', NULL, NULL, 0, 0,
                                 norm, norm, red, norm, NULL);
                    iconv_close(cd);
                }
                else /* successful regcomp() */
                {
                    /* search member array for match */
                    int i = 0;
                    char matchstring[51];
                    
                    if (regexOn == true)
                    {
                        free(regexsubset);
                        QsnWrtPad(' ', 60, 0, MBRLIST_STRROW - 2, strcol, cmdbuf, *mbrwin, 
                        	NULL);
                        regexOn = false;
                        reload = true;
                    }

                    for (i; i < nbrMbrs; i++)
                    {
                        memset(matchstring, ' ', sizeof(matchstring));
                        if (fldqry->row == 5)
                        {
                            memcpy(matchstring, pmbrnames[i].mbrtxt, 
                            	sizeof(pmbrnames->mbrtxt));
                        }
                        else
                        {
                            memcpy(matchstring, pmbrnames[i].membername, 
                            	sizeof(pmbrnames->membername));
                        }
                        matchstring[triml(matchstring, ' ')] = '\0';
                        if ((rc = regexec(&preg, matchstring, nmatch, pmatch, 0)) == 0)
                        {                            
                            nbrmatches++;
                            if (nbrmatches == 1)
                            {
                                regexsubset = malloc(nbrMbrs * sizeof(*pmbrnames));
                                if (regexsubset == NULL)
                                {
                                    fprintf(stderr, "Out of memory\n");
                                    exit(EXIT_FAILURE);
                                }
                            }
                            regexsubset[nbrmatches - 1] = pmbrnames[i];
                        }
                    }

                    if (nbrmatches > 0)
                    {
                        char s[60];

                        regexOn = true;
                        snprintf(s, sizeof(s), "Nbr matches with pattern '%s': %d", 
                        	pattern, nbrmatches);
                        QsnWrtDta(s, strlen(s), 0, MBRLIST_STRROW - 2, strcol, norm, norm, 
                        	green, norm, cmdbuf, *mbrwin, NULL);
                        idx = 0;
                        firstpage = true;
                        lastpage = false;
                        loadMemberList(*mbrwin, regexsubset, nbrmatches, &idx, strrow, 
                        	strcol, &currow);
                    }
                    else
                    {
                        char s[50];
                        snprintf(s, sizeof(s), "No matches with pattern '%s'", pattern);
                        QsnPutWinMsg(*mbrwin, s, strlen(s), '0', NULL, NULL, 0, 0,
                                     norm, norm, red, norm, NULL);
                        if (reload == true)
                        {
                            reload = false;
                            QsnWrtPad(' ', 60, 0, MBRLIST_STRROW - 2, strcol, cmdbuf, 
                            	*mbrwin, NULL);
                            idx = 0;
                            firstpage = true;
                            lastpage = false;
                            loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, 
                            	strcol, &currow);
                        }
                    }
                }
                regfree(&preg);
                setlocale(LC_CTYPE, ""); /* reset to default */
            }
        }
        /* remove regex filter */
        else if (aid == QSN_F7 && regexOn == true)
        {
            free(regexsubset);
            regexOn = false;
            QsnWrtPad(' ', 60, 0, MBRLIST_STRROW - 2, strcol, cmdbuf, *mbrwin, NULL);
            idx = 0;
            firstpage = true;
            lastpage = false;
            loadMemberList(*mbrwin, pmbrnames, nbrMbrs, &idx, strrow, strcol, &currow);
        }
        
        strrow = MBRLIST_STRROW;
        QsnWTD(cc1, cc2, cmdbuf, 0, NULL);
    }
    /* F12 pressed. */
    if (regexOn == true)
    {
        free(regexsubset);
    }
    QsnEndWin(*mbrwin, '1', NULL);
    QsnClrBuf(cmdbuf, NULL);
    return 0;
}


void loadMemberList(Qsn_Win_T mbrwin,
                    mbrnames *pmbrnames,
                    short nbrMbrs,
                    int *idx,
                    int row,
                    int strcol,
                    int *currow)
{
    int i = 0;
    char s [68]; /* nbr + ' ' + membername + ' ' + txt */

    for (i; i < LISTSIZE && *idx < nbrMbrs; i++, (*idx)++)
    {
        snprintf(s, sizeof(s), "%5d %.*s %.*s", *idx, sizeof(pmbrnames->membername), pmbrnames[*idx].membername,
                 sizeof(pmbrnames->mbrtxt), pmbrnames[*idx].mbrtxt);
        QsnWrtDta(s, sizeof(s), 0, row + i, strcol, norm, norm,
                  pink, pink, cmdbuf, mbrwin, NULL);
    }
    *currow = i;

    if (i < LISTSIZE)
    {
        for (i; i < LISTSIZE; i++)
        {
            QsnWrtPad(' ', sizeof(s), 0, row + i, strcol, cmdbuf, mbrwin, NULL);
        }
    }
    /* skriv om det finns fler rader etc helst sen */
    return;
}


/*
 * create the database relations array
 */
int createDBRList (char *filename, char *usname, dbrinfo_t **pdbrdata, int *nbrDBRs)
{
    Qus_EC_t userror;
    int i = 0;
    Qdb_Ldbm_Header_t   *field_hdr;
    Qdb_Dbrl0100_t      *field_list;
    dbrinfo_t           *p;
    char                *noDBR = "*NONE",
                         *hdr_section,
                         *list_section;
    userror.Bytes_Provided = 0;
    QDBLDBR(usname, "DBRL0100", filename, "*FIRST    ", "          ", &userror);

    /* ptr to userspace is a global already  */
    char_space = (char *)space;
    list_section = char_space;
    hdr_section = list_section + space->Offset_Header_Section;
    field_hdr = (Qdb_Ldbm_Header_t *) hdr_section;
    list_section = list_section + space->Offset_List_Data;
    field_list = (Qdb_Dbrl0100_t *) list_section;

    *nbrDBRs = space->Number_List_Entries;
    p = malloc(sizeof(dbrinfo_t) * space->Number_List_Entries);
    if (space->Number_List_Entries == 1 && memcmp(field_list->Dependent_File_Name, noDBR, strlen(noDBR)) == 0)
    {
        memcpy(&p[0], field_list, sizeof(*field_list)); /* don't need to get keys */
        *nbrDBRs = 0;
        p[0].nbrkeys = 0;
    }
    else
    {
        for (i; i < space->Number_List_Entries; i++)
        {
            /* keydata  */
            char qrfilename [20];   /* output from API */
            char filename   [20];   /* what we are passing */
            char error_code [201];
            char *allowed = " ",
                  *fileauths = "*READ     *OBJOPR   *EXECUTE  ";
            int  nbrAuthorities = 3,
                 zero = 0;
            Qus_EC_t        *err;
            Qdb_Qdbfh_t     dbfdata;

            memset(&p[i], ' ', sizeof(p[i]));            
            memcpy(&p[i], field_list, sizeof(*field_list));

            memcpy(filename, field_list->Dependent_File_Name, 10);
            memcpy(filename + 10, field_list->Dependent_File_Library_Name, 10);
            err = (Qus_EC_t *) &error_code[0];
            memset(err, 0, sizeof(*err));
            err->Bytes_Provided == sizeof(error_code) - 1;

            /* if we are not authorised to the file, put "*** Not authorized ****"  across the dependency and constraint positions */
            QSYCUSRA(allowed, "*CURRENT  ", filename, "*FILE     ", fileauths, 
            	&nbrAuthorities, &zero, err);
            if (memcmp(allowed, "N", 1) == 0)
            {
                char *s = "** Not authorized **";
                memcpy(p[i].filetextdesc, s, strlen(s));
                p[i].nbrkeys = 0;
                p[i].authorized = false;
                continue;
            }
            else
            {
                p[i].authorized = true;
            }

            /* Fix. Save accesspath too, would be nice to be able to show if unique key or not */
            QDBRTVFD(&dbfdata, sizeof(dbfdata), qrfilename, "FILD0100", filename, 
            	"*FIRST    ", "0", "*LCL      ", "*EXT      ", err);

            memcpy(p[i].filetextdesc, &dbfdata.Qdbfhtx.Qdbfhtxt, 
            	sizeof(dbfdata.Qdbfhtx.Qdbfhtxt));
            if (dbfdata.Qdbfhflg.Qdbfhfky == 1) /* keyed */
            {
                short j = 0,
                      keystrpos = 0;
                Qdb_Qdbwh_t     *keydta;
                Qdb_Qdbwhkey_t  *dbrkeys;

                /* access fddata->Qdbfpact */
                keydta = (Qdb_Qdbwh_t *) malloc(sizeof(Qdb_Qdbwh_t));
                QDBRTVFD(keydta, sizeof(Qdb_Qdbwh_t), qrfilename, "FILD0300", filename,
                         "*FIRST    ", "0", "*LCL      ", "*EXT      ", err);            
                dbrkeys = (Qdb_Qdbwhkey_t *) ((char *) keydta + keydta->Rec_Key_Info->Key_Info_Offset);
                p[i].nbrkeys = keydta->Rec_Key_Info->Num_Of_Keys;
                memset(p[i].keystr, ' ', sizeof(p[i].keystr));
                for (j; j < keydta->Rec_Key_Info->Num_Of_Keys; j++)
                {
                    char  *pch;
                    int   cpylen;

                    /* create comma separated string of the keys  */
                    pch = memchr(dbrkeys[j].Int_Field_Name, ' ', 10);
                    cpylen = (pch == NULL ? 10 : pch - dbrkeys[j].Int_Field_Name);
                    memcpy(p[i].keystr + keystrpos, dbrkeys[j].Int_Field_Name, cpylen);
                    keystrpos += cpylen + 1;
                    p[i].keystr[keystrpos - 1] = ',';

                    /* handle if keystrpos is larger than we space for */
                    if (keystrpos > sizeof(p[i].keystr) - 10)
                    {
                        memcpy(p[i].keystr + keystrpos, "...more", 7);
                    }
                }
                p[i].keystr[keystrpos - 1] = ' '; /* ta bort sista kommatecknet */
                free(keydta);
            }
            else /* physical accesspath */
            {
                char *s = "No keys";

                p[i].nbrkeys = 0;
                memcpy(p[i].keystr, s, strlen(s));
            }
            field_list = (Qdb_Dbrl0100_t *) ((char *) field_list + space->Size_Each_Entry);
        }
    }
    *pdbrdata = p;
    return 1;
}


/*
 * display database relations window
 */
int displayDBRWindow (Qsn_Win_T *dbrwin,
                      dbrinfo_t *dbrdata,
                      char      *selectedFile,
                      char      *filename,
                      int        nbrDBRs)
{
    char *winDBRText [] = {"  Database Relations  ",
                           "F2 Fold/Unfold F12=Previous",
                           "File: ",
                           "Lib : ",
                           "Nbr related files:"
                          };

    char *winmessages [] = {"Cursor not in the list. Try again",
                            "You're not authorized to that file, sorry"
                           };
    Qsn_Win_Desc_T win_desc;
    Q_Bin4         win_desc_length = sizeof(win_desc);
    const int strrow = MBRLIST_STRROW;
    const int strcol = 5;
    int idx = 0;
    int currow;
    int lastpage = false;
    int firstpage = true;
    static int fold = false;
    short dbrtxtcol2 = 23;
    char aid;
    char headlines1[70];
    /* inputData *pinput;  is a global */
    const Q_Uchar cc1 = QSN_CC1_MDTALL_CLRALL,
                  cc2 = QSN_CC2_UNLOCKBD;

    snprintf(headlines1, sizeof(headlines1), "%-*.*s %-*.*s %s",
             sizeof(dbrdata[idx].dbrinfo.Dependent_File_Name),
             sizeof(dbrdata[idx].dbrinfo.Dependent_File_Name),
             "Dep.File",
             sizeof(dbrdata[idx].dbrinfo.Dependent_File_Library_Name),
             sizeof(dbrdata[idx].dbrinfo.Dependent_File_Library_Name),
             "Library",
             "Text");
    QsnInzWinD(&win_desc, win_desc_length, NULL);
    win_desc.fullscreen = '1';
    win_desc.GUI_support = '0';
    *dbrwin = QsnCrtWin(&win_desc, win_desc_length, NULL, 0, '0', NULL, 0, NULL, NULL);

    /* create headline here, not in the load list func */
    QsnWrtDta(winDBRText[0], strlen(winDBRText[0]), 0, 2, 55, norm, norm, QSN_SA_BLU_RI, 
    	norm, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(headlines1, strlen(headlines1), 0, strrow - 1, strcol, norm, norm,
              QSN_SA_WHT_UL, norm, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(winDBRText[2], strlen(winDBRText[2]), 0, 3, strcol, norm, norm,
              pink, pink, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(filename, 10, 0, 3, 12, norm, norm,
              white, white, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(winDBRText[3], strlen(winDBRText[3]), 0, 4, strcol, norm, norm,
              pink, pink, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(filename + 10, 10, 0, 4, 12, norm, norm, white, white, cmdbuf, *dbrwin, NULL);
    QsnWrtDta(winDBRText[4], strlen(winDBRText[4]), 0, 3, dbrtxtcol2, norm, norm,
              pink, pink, cmdbuf, *dbrwin, NULL);
    snprintf(headlines1, sizeof(headlines1) - 1, "%d", nbrDBRs);
    QsnWrtDta(headlines1, strlen(headlines1), 0, 3, dbrtxtcol2 + strlen(winDBRText[4]) + 1, 
    	norm, norm, white, white, cmdbuf, *dbrwin, NULL);

    /* fkeys     */
    QsnWrtDta(winDBRText[1], strlen(winDBRText[1]), 0, 26, strcol, norm, norm,
              blue, blue, cmdbuf, *dbrwin, NULL);

    loadDBRList(*dbrwin, dbrdata, nbrDBRs, &idx, strrow, strcol, &currow, fold);
    QsnStrWin(*dbrwin, '1', NULL);
    QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
               cmdbuf, envhandle, NULL);
    QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
    pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    while (pinput->AID != QSN_F12)
    {
        Q_Bin4 csrrow = 0;

        QsnClrBuf(cmdbuf, NULL);
        QsnClrWinMsg(*dbrwin, NULL);
        QsnGetCsrAdr(&csrrow, NULL, *dbrwin, NULL);
        if (pinput->AID == QSN_F2)
        {
            int k = 0;
            /* count backwards where to start loading the list  */
            if (fold == true)
            {
                idx -= (idx != nbrDBRs ? LISTSIZE : nbrDBRs % LISTSIZE);
                fold = false;
            }
            else
            {
                idx -= (idx <= nbrDBRs ? LISTSIZE / 3 : nbrDBRs % (LISTSIZE / 3) );
                /* not pretty     */
                if (idx < 0)
                {
                    idx = 0;
                }
                fold = true;
            }
            for (k; k < LISTSIZE; k++)
            {
                QsnWrtPad(' ', LISTWIDTH, 0, strrow + k, 1, cmdbuf, *dbrwin, NULL);
            }
            loadDBRList(*dbrwin, dbrdata, nbrDBRs, &idx, strrow, strcol, &currow, fold);
        }
        else if (pinput->AID == QSN_PAGEDOWN && lastpage == false)
        {
            firstpage = false;
            if (idx == nbrDBRs)
            {
                lastpage = true;
            }
            else
            {
                loadDBRList(*dbrwin, dbrdata, nbrDBRs, &idx, strrow, strcol, &currow, fold);
            }
        }
        else if (pinput->AID == QSN_PAGEUP && firstpage == false)
        {
            if (currow - LISTSIZE < 0)
            {
                idx -= (idx % (fold == true ? 
                	LISTSIZE : LISTSIZE / 3)) + (fold == true ? LISTSIZE : LISTSIZE / 3);
            }
            else
            {
                idx -= (fold == true ? LISTSIZE * 2 : (LISTSIZE / 3) * 2);
            }

            lastpage = false;
            if (idx <= 0)
            {
                firstpage = true;
                idx = 0;
            }
            loadDBRList(*dbrwin, dbrdata, nbrDBRs, &idx, strrow, strcol, &currow, fold);
        }
        /* select file back */
        if (pinput->AID == QSN_F9)
        {
            if (csrrow >= strrow && csrrow < strrow + (currow < LISTSIZE ? currow : LISTSIZE))
            {
                int selectedidx = -1;

                if (fold == true)
                {
                    if (currow == LISTSIZE && csrrow >= strrow && csrrow < strrow + LISTSIZE)
                    {
                        selectedidx = (idx - LISTSIZE) + csrrow - strrow;
                    }
                    else if (currow < LISTSIZE && csrrow >= strrow && csrrow < strrow + currow)
                    {
                        selectedidx = (idx - currow) + (csrrow - strrow);
                    }
                }
                else
                {
                    if (currow == LISTSIZE && csrrow >= strrow && csrrow < strrow + LISTSIZE)
                    {
                        selectedidx = ((idx * 3 - LISTSIZE) + (csrrow - strrow)) / 3;
                    }
                    else if (currow < LISTSIZE && csrrow >= strrow && csrrow < strrow + currow)
                    {
                        selectedidx = ((idx * 3 - currow) + (csrrow - strrow)) / 3;
                    }
                }
                /* return selected file name in file lib form, if authorised  */
                if (dbrdata[selectedidx].authorized == true)
                {
                    memcpy(selectedFile, dbrdata[selectedidx].dbrinfo.Dependent_File_Name, 
                    	10);
                    memcpy(selectedFile + 10, 
                    	dbrdata[selectedidx].dbrinfo.Dependent_File_Library_Name, 10);
                    QsnEndWin(*dbrwin, '1', NULL);
                    QsnClrBuf(cmdbuf, NULL);
                    return 1;
                }
                else
                {
                    QsnPutWinMsg(*dbrwin, winmessages[1], strlen(winmessages[1]), '0', 
                    	NULL, NULL, 0, 0, norm, norm, red, norm, NULL);
                }
            }
            else
            {
                QsnPutWinMsg(*dbrwin, winmessages[0], strlen(winmessages[0]), '0', NULL, 
                			NULL, 0, 0, norm, norm, red, norm, NULL);
            }
        }
        QsnReadInp(QSN_CC1_MDTALL_CLRALL, QSN_CC2_UNLOCKBD, 0, 0,
                   cmdbuf, envhandle, NULL);
        QsnPutGetBuf(cmdbuf, inputbuf, envhandle, NULL);
        pinput = (inputData *) QsnRtvDta(inputbuf, NULL, NULL);
    }
    QsnEndWin(*dbrwin, '1', NULL);
    QsnClrBuf(cmdbuf, NULL);
    return 0;
}


/*
 * write dbr info to the cmdbuffer used for list 
 */
void loadDBRList(Qsn_Win_T  dbrwin,
                 dbrinfo_t *dbrdata,
                 int        nbrDBRs,
                 int       *idx,
                 int        row,
                 int        strcol,
                 int       *currow,
                 int        fold)
{
    int i = 0;
    int listrows;
    char s [90];

    /* showing one or three rows */
    listrows = LISTSIZE;
    for (i; i < listrows && *idx < nbrDBRs; i++, (*idx)++)
    {
        char dep[11];
        short fnamesize = sizeof(dbrdata[*idx].dbrinfo.Dependent_File_Name),
              flibsize = sizeof(dbrdata[*idx].dbrinfo.Dependent_File_Library_Name);

        /* add more later, constraints JREF? */
        memset(dep, ' ', sizeof(dep));
        memset(s, ' ', sizeof(s) - 1);
        /* Note. if len in snprintf was strlen(s) there was always some junk in the first
           row, therefore the hassle  */
        snprintf(s, fnamesize + flibsize + 1, "%.*s %.*s %.*s %.*s",
                 fnamesize,
                 dbrdata[*idx].dbrinfo.Dependent_File_Name,
                 flibsize,
                 dbrdata[*idx].dbrinfo.Dependent_File_Library_Name);

        QsnWrtDta(s, strlen(s), 0, row + i, strcol, norm, norm, QSN_SA_TRQ_CS, norm, 
        	cmdbuf, dbrwin, NULL);
        memset(s, ' ', sizeof(s) - 1);
        snprintf(s, sizeof(s) - 1, "%.*s", sizeof(dbrdata[*idx].filetextdesc), 
        	dbrdata[*idx].filetextdesc);
        if (*s != '\0') /* null crashes the API so check before */
        {
            if (dbrdata[*idx].authorized == true)
                QsnWrtDta(s, strlen(s), 0, row + i, strcol + fnamesize + flibsize + 2,
                          norm, norm, white, white, cmdbuf, dbrwin, NULL);
            else
            {
                QsnWrtDta(s, strlen(s), 0, row + i, strcol + fnamesize + flibsize + 2, 
                	norm, norm, QSN_SA_RED_BL, norm, cmdbuf, dbrwin, NULL);
            }
        }
        if (fold == false)
        {
            if (dbrdata[*idx].authorized == true)
            {
                if (memcmp(dbrdata[*idx].dbrinfo.Dependency_Type, "D", 1) == 0)
                {
                    strncpy(dep, "Data", sizeof(dep));
                }
                else if (memcmp(dbrdata[*idx].dbrinfo.Dependency_Type, "C", 1) == 0)
                {
                    strncpy(dep, "Constraint", sizeof(dep));
                }
                else if (memcmp(dbrdata[*idx].dbrinfo.Dependency_Type, "V", 1) == 0)
                {
                    strncpy(dep, "View dep", sizeof(dep));
                }
                else /* O, I, blank, vad kalla dem annars? */
                {
                    strncpy(dep, dbrdata[*idx].dbrinfo.Dependency_Type, 1);
                }
            }
            /* row 2 */
            i++;
            memset(s, ' ', sizeof(s) - 1);
            snprintf(s, sizeof(s), "%.10s %.50s", dep, 
            	dbrdata[*idx].dbrinfo.Constraint_Name);
            if (*s != '\0')
            {
                QsnWrtDta(s, sizeof(s) - 1, 0, row + i, strcol + 2, norm, norm, white, 
                	white, cmdbuf, dbrwin, NULL);
            }
            /* row 3 */
            i++;
            memset(s, ' ', sizeof(s) - 1);
            if (dbrdata[*idx].nbrkeys > 0)
            {
                snprintf(s, sizeof(s), "Keys(%d): %.*s", dbrdata[*idx].nbrkeys, 70, 
                	dbrdata[*idx].keystr);    
            }
            else
            {
                snprintf(s, sizeof(s), "%s", dbrdata[*idx].keystr);
            }
            if (*s != '\0')
            {
                QsnWrtDta(s, strlen(s), 0, row + i, strcol + 2, norm, norm, white, white, 
                	cmdbuf, dbrwin, NULL);
            }
        }
    }
    *currow = i;

    /* todo. think through how this works in unfold, do not pad too much/little */
    if (i <= listrows && *idx == nbrDBRs)
    {
        for (i; i < listrows; i++)
        {
            QsnWrtPad(' ', LISTWIDTH, 0, row + i, 1, cmdbuf, dbrwin, NULL);
        }
        snprintf(s, sizeof(s), "%s", "No more relations to display");
        QsnWrtDta(s, strlen(s), 0, 25, 80, norm, norm, blue, blue, cmdbuf, dbrwin, NULL);
    }
    else
    {
        snprintf(s, sizeof(s) , "%s", "Page down for more         ");
        QsnWrtDta(s, strlen(s), 0, 25, 80, norm, norm, blue, blue, cmdbuf, dbrwin, NULL);
    }
    return;
}


void cleanUp()
{
    short i;

    free(fptr);
    for (i = 0; i < LISTSIZE; i++)
    {
        free(listData[i]);
    }
    QsnDltEnv(envhandle, NULL);
    QsnDltBuf(cmdbuf, NULL);
    QsnDltBuf(inputbuf, NULL);
    return;
}
