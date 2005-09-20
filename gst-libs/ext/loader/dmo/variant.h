#ifndef DMO_VARIANT_H
#define DMO_VARIANT_H

/* Different types of variant data */
enum VARENUM {
    VT_EMPTY = 0,
    VT_NULL = 1,
    VT_I2 = 2,
    VT_I4 = 3,
    VT_R4 = 4,
    VT_R8 = 5,
    VT_CY = 6,
    VT_DATE = 7,
    VT_BSTR = 8,
    VT_DISPATCH = 9,
    VT_ERROR = 10,
    VT_BOOL = 11,
    VT_VARIANT = 12,
    VT_UNKNOWN = 13,
    VT_DECIMAL = 14,
    VT_I1 = 16,
    VT_UI1 = 17,
    VT_UI2 = 18,
    VT_UI4 = 19,
    VT_I8 = 20,
    VT_UI8 = 21,
    VT_INT = 22,
    VT_UINT = 23,
    VT_VOID = 24,
    VT_HRESULT = 25,
    VT_PTR = 26,
    VT_SAFEARRAY = 27,
    VT_CARRAY = 28,
    VT_USERDEFINED = 29,
    VT_LPSTR = 30,
    VT_LPWSTR = 31,
    VT_RECORD = 36,
    VT_INT_PTR = 37,
    VT_UINT_PTR = 38,
    VT_FILETIME = 64,
    VT_BLOB = 65,
    VT_STREAM = 66,
    VT_STORAGE = 67,
    VT_STREAMED_OBJECT = 68,
    VT_STORED_OBJECT = 69,
    VT_BLOB_OBJECT = 70,
    VT_CF = 71,
    VT_CLSID = 72,
    VT_BSTR_BLOB = 0xfff,
    VT_VECTOR = 0x1000,
    VT_ARRAY = 0x2000,
    VT_BYREF = 0x4000,
    VT_RESERVED = 0x8000,
    VT_ILLEGAL = 0xffff,
    VT_ILLEGALMASKED = 0xfff,
    VT_TYPEMASK = 0xfff
};

#define __VARIANT_NAME_1 n1
#define __VARIANT_NAME_2 n2
#define __VARIANT_NAME_3 n3
#define __VARIANT_NAME_4 brecVal

typedef const WCHAR *LPCOLESTR;
typedef unsigned short *BSTR;
typedef unsigned short VARTYPE;
typedef short VARIANT_BOOL;

/* CURRENCY */
typedef union tagCY {
    struct {
#ifdef WORDS_BIGENDIAN
        long  Hi;
        unsigned long Lo;
#else
        unsigned long Lo;
        long  Hi;
#endif
    } DUMMYSTRUCTNAME;
    long long int64;
} CY;

/* DECIMAL */
typedef struct tagDEC {
  unsigned short wReserved;
  union {
    struct {
      BYTE scale;
      BYTE sign;
    } DUMMYSTRUCTNAME;
    unsigned short signscale;
  } DUMMYUNIONNAME;
  unsigned long Hi32;
  union {
    struct {
#ifdef WORDS_BIGENDIAN
      unsigned long Mid32;
      unsigned long Lo32;
#else
      unsigned long Lo32;
      unsigned long Mid32;
#endif
    } DUMMYSTRUCTNAME1;
    unsigned long long Lo64;
  } DUMMYUNIONNAME1;
} DECIMAL;

typedef struct tagVARIANT VARIANT;
struct tagVARIANT {
    union {
        struct __tagVARIANT {
            VARTYPE vt;
            WORD wReserved1;
            WORD wReserved2;
            WORD wReserved3;
            union {
                signed char cVal;
                USHORT uiVal;
                ULONG ulVal;
                INT intVal;
                UINT uintVal;
                BYTE bVal;
                SHORT iVal;
                LONG lVal;
                FLOAT fltVal;
                DOUBLE dblVal;
                VARIANT_BOOL boolVal;
                SCODE scode;
                DATE date;
                BSTR bstrVal;
                CY cyVal;
                void *punkVal;
                void *pdispVal;
                SAFEARRAY *parray;
                LONGLONG llVal;
                ULONGLONG ullVal;
                signed char *pcVal;
                USHORT *puiVal;
                ULONG *pulVal;
                INT *pintVal;
                UINT *puintVal;
                BYTE *pbVal;
                SHORT *piVal;
                LONG *plVal;
                FLOAT *pfltVal;
                DOUBLE *pdblVal;
                VARIANT_BOOL *pboolVal;
                SCODE *pscode;
                DATE *pdate;
                BSTR *pbstrVal;
                VARIANT *pvarVal;
                void *byref;
                CY *pcyVal;
                DECIMAL *pdecVal;
                void **ppunkVal;
                void **ppdispVal;
                SAFEARRAY **pparray;
                LONGLONG *pllVal;
                ULONGLONG *pullVal;
                struct __tagBRECORD {
                    void *pvRecord;
                    void *pRecInfo;
                } __VARIANT_NAME_4;
            } __VARIANT_NAME_3;
        } __VARIANT_NAME_2;
        DECIMAL decVal;
    } __VARIANT_NAME_1;
};


#endif /* DMO_VARIANT_H */
