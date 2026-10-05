/** @file
  On-disk layout of the AORUS setup database (setup.sdb) and image pack
  (gui.pak).  Produced by tools/sdb.py and tools/assets.py; keep in sync.

  SPDX-License-Identifier: MIT
**/

#ifndef SDB_FORMAT_H_
#define SDB_FORMAT_H_

#include <Uefi.h>

#define SDB_VERSION  1

#pragma pack(1)

typedef struct {
  CHAR8     Magic[8];             // "AORUSSDB"
  UINT32    Version;
  UINT32    TotalSize;
  UINT32    SectionCount;
  UINT32    Reserved;
} SDB_HEADER;

typedef struct {
  CHAR8     Tag[4];
  UINT32    Offset;               // from the start of the file
  UINT32    Size;
  UINT32    Count;
} SDB_SECTION;

typedef struct {
  CHAR8     Code[8];              // RFC 4646 code, e.g. "en-US"
  UINT32    MaxId;
  UINT32    TableOffset;          // UINT32[MaxId + 1] from the LANG section start
  CHAR16    NativeName[8];        // e.g. L"English"
} SDB_LANG;

#define SDB_VAR_HAS_DEFAULTS  0x0001  // factory defaults present (persistent)
#define SDB_VAR_VOLATILE      0x0002  // runtime state, never saved

typedef struct {
  UINT16      Id;
  UINT16      Flags;
  UINT32      Size;
  EFI_GUID    Guid;
  CHAR16      Name[40];
  UINT32      DataOffset;         // default contents, from the VDAT section start
  UINT32      Attributes;
} SDB_VAR;

typedef struct {
  UINT16    FormId;
  UINT16    Title;
  UINT32    First;                // first statement index
  UINT32    Count;
} SDB_FORM;

// Statement kinds
#define SDB_K_SUBTITLE  1
#define SDB_K_TEXT      2
#define SDB_K_REF       3
#define SDB_K_ONEOF     4
#define SDB_K_CHECKBOX  5
#define SDB_K_NUMERIC   6
#define SDB_K_STRING    7
#define SDB_K_PASSWORD  8
#define SDB_K_ACTION    9
#define SDB_K_DATE      10
#define SDB_K_TIME      11
#define SDB_K_RESET     12
#define SDB_K_ORDERED   13

// Statement flags
#define SDB_SF_HAS_DEFAULT       0x01
#define SDB_SF_FAVORITE_DEFAULT  0x02
#define SDB_SF_INDENT            0x04
#define SDB_SF_ORANGE            0x08
#define SDB_SF_ACTION            0x10
#define SDB_SF_VIRTUAL           0x20

// Numeric display formats
#define SDB_NF_DEC          0
#define SDB_NF_HEX          1
#define SDB_NF_AUTO         2     // 0 = "Auto", otherwise decimal
#define SDB_NF_AUTO_MV      3     // 0 = "Auto", otherwise millivolts -> "1.200V"
#define SDB_NF_AUTO_MV_OFS  4     // 0 = "Auto", otherwise signed millivolts -> "+0.050V"
#define SDB_NF_AUTO_CLK     5     // 0 = "Auto", otherwise 10 kHz units -> "100.00MHz"

#define SDB_NO_EXPR  0xFFFFFFFF
#define SDB_NO_VAR   0xFFFF

// IFR question flags
#define SDB_QF_READ_ONLY       0x01
#define SDB_QF_CALLBACK        0x04
#define SDB_QF_RESET_REQUIRED  0x10

typedef struct {
  UINT64    Min;
  UINT64    Max;
  UINT64    Step;
  UINT64    Default;
  UINT32    OptFirst;
  UINT32    Suppress;             // expression offset or SDB_NO_EXPR
  UINT32    GrayOut;
  UINT32    Reserved;
  UINT16    Prompt;
  UINT16    Help;
  UINT16    Text2;
  UINT16    QuestionId;
  UINT16    VarIndex;
  UINT16    VarOffset;
  UINT16    Ref;                  // target form of a REF
  UINT16    OptCount;
  UINT8     Kind;
  UINT8     QFlags;
  UINT8     Width;
  UINT8     NumFlags;
  UINT8     Dyn;                  // SDB_DYN_* (runtime generated text)
  UINT8     Flags;                // SDB_SF_*
  UINT8     Action;               // SDB_ACT_*
  UINT8     NumFmt;               // SDB_NF_*
} SDB_STMT;

typedef struct {
  UINT64    Value;
  UINT16    Text;
  UINT8     Flags;
  UINT8     Pad;
  UINT32    Suppress;
} SDB_OPT;

typedef struct {
  UINT16    Title;
  UINT16    FormId;
  UINT16    Icon;
  UINT16    Pad;
} SDB_TAB;

typedef struct {
  UINT16    Key;
  UINT16    Pad;
  UINT32    Stmt;
} SDB_SPEC;

typedef struct {
  CHAR16    Model[32];
  CHAR16    BiosVersion[32];
  CHAR16    BiosDate[32];
  CHAR16    BiosId[32];
  CHAR16    BoardShort[32];
} SDB_INFO;

// Expression bytecode
#define SDB_E_END     0x00
#define SDB_E_PUSH    0x01        // UINT64
#define SDB_E_LOAD    0x02        // UINT16 var, UINT16 offset, UINT8 width
#define SDB_E_INLIST  0x03        // UINT16 count, UINT16 values[count]
#define SDB_E_EQ      0x10
#define SDB_E_NE      0x11
#define SDB_E_LT      0x12
#define SDB_E_LE      0x13
#define SDB_E_GT      0x14
#define SDB_E_GE      0x15
#define SDB_E_AND     0x20
#define SDB_E_OR      0x21
#define SDB_E_NOT     0x22
#define SDB_E_BAND    0x30
#define SDB_E_BOR     0x31
#define SDB_E_BNOT    0x32
#define SDB_E_ADD     0x33
#define SDB_E_SUB     0x34
#define SDB_E_MUL     0x35
#define SDB_E_DIV     0x36
#define SDB_E_MOD     0x37
#define SDB_E_SHL     0x38
#define SDB_E_SHR     0x39
#define SDB_E_COND    0x40

// Image pack
typedef struct {
  CHAR8     Magic[8];             // "AORUSPAK"
  UINT32    Version;
  UINT32    Count;
} PAK_HEADER;

typedef struct {
  UINT32    Id;
  UINT16    Width;
  UINT16    Height;
  UINT32    Offset;               // BGRA pixels, from the start of the pack
  UINT32    Size;
} PAK_ENTRY;

#pragma pack()

// Fixed image ids (tools/assets.py)
#define IMG_BOOT_LOGO          1
#define IMG_POST_BANNER        2
#define IMG_POST_BANNER_SMALL  3
#define IMG_AMI_LOGO           4
#define IMG_GUI(n)  (100 + (n))

#endif
