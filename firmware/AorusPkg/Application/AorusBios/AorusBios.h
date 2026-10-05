/** @file
  AORUS BIOS emulator - shared declarations.

  SPDX-License-Identifier: MIT
**/

#ifndef AORUS_BIOS_H_
#define AORUS_BIOS_H_

#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextInEx.h>
#include <Guid/FileInfo.h>
#include <Guid/GlobalVariable.h>

#include <SdbFormat.h>
#include <SdbIds.h>

extern EFI_GUID  gAorusSdbFileGuid;
extern EFI_GUID  gAorusGuiFileGuid;
extern EFI_GUID  gAorusFontFileGuid;
extern EFI_GUID  gAorusEmuVariableGuid;

//
// ---------------------------------------------------------------- Graphics
//
#define SCREEN_W  1920
#define SCREEN_H  1080

#define RGB(r, g, b)  ((UINT32)(((UINT32)(r) << 16) | ((UINT32)(g) << 8) | (UINT32)(b)))

typedef struct {
  UINT16    Width;
  UINT16    Height;
  UINT32    *Pixels;                // BGRA, straight alpha (A in bits 24-31)
} IMAGE;

typedef struct {
  INT32    X;
  INT32    Y;
  INT32    W;
  INT32    H;
} RECT;

extern UINT32  *gBack;              // SCREEN_W x SCREEN_H back buffer (0x00RRGGBB)

EFI_STATUS
GfxInit (
  VOID
  );

VOID
GfxFill (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color
  );

VOID
GfxBlend (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color,
  UINT32  Alpha
  );

VOID
GfxGradientV (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Top,
  UINT32  Bottom
  );

VOID
GfxGradientH (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Left,
  UINT32  Right
  );

VOID
GfxRect (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  UINT32  Color
  );

VOID
GfxLine (
  INT32   X0,
  INT32   Y0,
  INT32   X1,
  INT32   Y1,
  UINT32  Color,
  UINT32  Alpha
  );

VOID
GfxParallelogram (
  INT32   X,
  INT32   Y,
  INT32   W,
  INT32   H,
  INT32   Slant,
  UINT32  Top,
  UINT32  Bottom
  );

VOID
GfxDrawImage (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y
  );

VOID
GfxDrawImageAlpha (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  UINT32       Alpha
  );

VOID
GfxDrawImageTinted (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  UINT32       Color
  );

VOID
GfxDrawImageScaled (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  INT32        W,
  INT32        H
  );

VOID
GfxDrawImage3Slice (
  CONST IMAGE  *Img,
  INT32        X,
  INT32        Y,
  INT32        W,
  INT32        Cap
  );

VOID
GfxSetClip (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  );

VOID
GfxResetClip (
  VOID
  );

VOID
GfxPresent (
  VOID
  );

VOID
GfxPresentRect (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  );

UINT32 *
GfxSave (
  VOID
  );

VOID
GfxRestore (
  UINT32  *Saved
  );

EFI_STATUS
GfxSaveScreenshot (
  OUT CHAR16  *Name,
  IN  UINTN   NameSize
  );

//
// -------------------------------------------------------------------- Font
//
#define FONT_CELL   24
#define FONT_SCALE  256             // fixed point 1.0

EFI_STATUS
FontInit (
  VOID   *Data,
  UINTN  Size
  );

INT32
FontWidth (
  CONST CHAR16  *Text,
  UINT32        Scale
  );

INT32
FontDraw (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  );

INT32
FontDrawFit (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale,
  INT32         MaxWidth
  );

VOID
FontDrawCentered (
  INT32         Cx,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  );

VOID
FontDrawRight (
  INT32         Right,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale
  );

INT32
FontDrawWrapped (
  INT32         X,
  INT32         Y,
  CONST CHAR16  *Text,
  UINT32        Color,
  UINT32        Scale,
  INT32         MaxWidth,
  INT32         LineHeight,
  INT32         MaxLines
  );

//
// -------------------------------------------------------------------- Data
//
typedef struct {
  // file images
  UINT8       *Base;
  SDB_INFO    *Info;
  SDB_LANG    *Langs;
  UINT32      LangCount;
  UINT8       *LangBase;
  UINT8       *StrData;
  SDB_VAR     *Vars;
  UINT32      VarCount;
  UINT8       *VarDefaults;
  SDB_FORM    *Forms;
  UINT32      FormCount;
  SDB_STMT    *Stmts;
  UINT32      StmtCount;
  SDB_OPT     *Opts;
  UINT32      OptCount;
  UINT8       *Expr;
  SDB_TAB     *Tabs;
  UINT32      TabCount;
  SDB_SPEC    *Specs;
  UINT32      SpecCount;
  UINT16      *Named;
  UINT32      NamedCount;
  // runtime
  UINT8       **Cur;                // current values per varstore
  UINT8       **Prev;               // values when setup was entered (F5)
  UINT32      Lang;
  UINT32      EnLang;
} SDB_CONTEXT;

extern SDB_CONTEXT  gSdb;

EFI_STATUS
DataLoad (
  VOID
  );

CONST IMAGE *
DataImage (
  UINT32  Id
  );

CONST CHAR16 *
SdbStr (
  UINT16  Id
  );

CONST CHAR16 *
SdbNamed (
  UINT32  NamedId
  );

SDB_FORM *
SdbFindForm (
  UINT16  FormId
  );

SDB_STMT *
SdbSpecial (
  UINT16  Key
  );

SDB_STMT *
SdbVisibleVariant (
  SDB_STMT  *S
  );

SDB_STMT *
SdbSpecialVisible (
  UINT16  Key
  );

UINT64
SdbSpecialValue (
  UINT16  Key,
  UINT64  Fallback
  );

VOID
SdbSetSpecialValue (
  UINT16  Key,
  UINT64  Value
  );

UINT64
SdbEval (
  UINT32  Offset
  );

BOOLEAN
SdbSuppressed (
  CONST SDB_STMT  *S
  );

BOOLEAN
SdbGrayed (
  CONST SDB_STMT  *S
  );

BOOLEAN
SdbOptionVisible (
  CONST SDB_OPT  *O
  );

UINT64
SdbGetValue (
  CONST SDB_STMT  *S
  );

VOID
SdbSetValue (
  CONST SDB_STMT  *S,
  UINT64          Value
  );

BOOLEAN
SdbChanged (
  CONST SDB_STMT  *S
  );

CONST SDB_OPT *
SdbFindOption (
  CONST SDB_STMT  *S,
  UINT64          Value
  );

VOID
SdbFormatValue (
  CONST SDB_STMT  *S,
  CHAR16          *Buf,
  UINTN           BufSize
  );

SDB_STMT *
SdbStmtByQuestion (
  UINT16  QuestionId
  );

//
// ---------------------------------------------------------------- Settings
//
#define MAX_FAVORITES  64

typedef struct {
  UINT32    Signature;               // 'AEMU'
  UINT32    Version;
  UINT32    MemTrainHash;
  UINT8     TrainCycle;
  UINT8     LastMode;                // 1 = Easy, 2 = Advanced
  UINT8     Language;
  UINT8     ResetPending;            // show "BIOS has been reset" after training
  UINT16    FavCount;
  UINT16    Favorites[MAX_FAVORITES];
  CHAR16    AdminPassword[24];
  CHAR16    UserPassword[24];
  UINT32    BootCount;
  UINT16    BootPriority[32];        // Boot#### numbers in our priority order
  UINT16    BootPriorityCount;
} AORUS_EMU_STATE;

#define AORUS_EMU_SIGNATURE  SIGNATURE_32 ('A', 'E', 'M', 'U')
#define AORUS_EMU_VERSION    1

extern AORUS_EMU_STATE  gEmu;
extern BOOLEAN          gCmosCleared;

EFI_STATUS
SettingsLoad (
  VOID
  );

EFI_STATUS
SettingsSave (
  VOID
  );

VOID
SettingsLoadDefaults (
  VOID
  );

VOID
SettingsSnapshot (
  VOID
  );

VOID
SettingsRestoreSnapshot (
  VOID
  );

BOOLEAN
SettingsDirty (
  VOID
  );

UINT32
SettingsMemoryHash (
  VOID
  );

VOID
EmuStateSave (
  VOID
  );

BOOLEAN
FavoriteHas (
  UINT16  QuestionId
  );

VOID
FavoriteToggle (
  UINT16  QuestionId
  );

//
// ------------------------------------------------------------------- Input
//
typedef struct {
  UINT16    Scan;
  CHAR16    Char;
  UINT32    Shift;                   // EFI_SHIFT_STATE flags
} KEY;

BOOLEAN
InputRead (
  OUT KEY  *Key,
  IN  UINTN TimeoutMs
  );

VOID
InputFlush (
  VOID
  );

//
// ------------------------------------------------------------------- HwMon
//
typedef struct {
  CHAR16    CpuBrand[49];
  UINT32    CpuSignature;
  UINT32    PCoreRatio;              // current (x100 MHz)
  UINT32    ECoreRatio;
  UINT32    BclkKhz10;               // 10 kHz units (10000 = 100.00 MHz)
  UINT32    CpuTempDeci;             // 0.1 °C
  UINT32    SysTempDeci;
  UINT32    PchTempDeci;
  UINT32    VrmTempDeci;
  UINT32    VcoreMv;
  UINT32    MemSizeMb;
  UINT32    MemMts;
  UINT32    CpuFanRpm;
  UINT32    SysFanRpm;
  UINT32    PumpRpm;
  UINT32    Tick;
  UINT8     Mac[6];
} HW_STATE;

extern HW_STATE  gHw;

VOID
HwInit (
  VOID
  );

VOID
HwTick (
  VOID
  );

VOID
HwDynText (
  UINT8   Dyn,
  CHAR16  *Buf,
  UINTN   BufSize
  );

VOID
HwFormatMv (
  UINT32  Mv,
  CHAR16  *Buf,
  UINTN   BufSize
  );

//
// -------------------------------------------------------------------- Boot
//
typedef struct {
  UINT16                          Number;
  CHAR16                          Name[96];
  EFI_BOOT_MANAGER_LOAD_OPTION    *Option;
} BOOT_ENTRY;

extern BOOT_ENTRY  *gBootEntries;
extern UINTN       gBootEntryCount;

VOID
BootRefresh (
  VOID
  );

BOOLEAN
BootTry (
  IN UINTN  Index
  );

BOOLEAN
BootDefault (
  VOID
  );

VOID
BootApplyPriority (
  VOID
  );

//
// -------------------------------------------------------------------- Post
//
VOID
PostRun (
  BOOLEAN  FromBds
  );

VOID
PostBootMenu (
  VOID
  );

//
// ------------------------------------------------------------------ Dialog
//
#define DLG_RESULT_CANCEL  ((INTN)-1)

INTN
DlgMenu (
  CONST CHAR16  *Title,
  CONST CHAR16  **Items,
  UINTN         Count,
  UINTN         Selected,
  BOOLEAN       Buttons
  );

BOOLEAN
DlgConfirm (
  CONST CHAR16  *Title,
  CONST CHAR16  *Message
  );

VOID
DlgMessage (
  CONST CHAR16  *Title,
  CONST CHAR16  *Message
  );

BOOLEAN
DlgInput (
  CONST CHAR16  *Title,
  CONST CHAR16  *Prompt,
  CHAR16        *Buf,
  UINTN         MaxChars,
  BOOLEAN       Password,
  BOOLEAN       NumericOnly
  );

VOID
DlgDrawFrame (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  CONST CHAR16  *Title
  );

//
// --------------------------------------------------------------------- UI
//
#define UI_EXIT_RESET  1
#define UI_EXIT_BOOT   2

UINTN
SetupRun (
  VOID
  );

VOID
QFlashRun (
  VOID
  );

VOID
SmartFanRun (
  VOID
  );

// Colours of the GIGABYTE light theme
#define C_ORANGE       RGB (0xF2, 0x65, 0x22)
#define C_ORANGE_DARK  RGB (0xC4, 0x4A, 0x10)
#define C_TEXT         RGB (0x3C, 0x3C, 0x3C)
#define C_TEXT_LIGHT   RGB (0x8A, 0x8A, 0x8A)
#define C_TEXT_GRAY    RGB (0xA8, 0xA8, 0xA8)
#define C_WHITE        RGB (0xFF, 0xFF, 0xFF)
#define C_BLACK        RGB (0x00, 0x00, 0x00)

#endif
