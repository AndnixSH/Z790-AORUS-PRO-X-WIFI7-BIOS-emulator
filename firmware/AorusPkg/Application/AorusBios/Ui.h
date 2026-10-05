/** @file
  Shared pieces of the setup UI (Easy Mode and Advanced Mode).

  SPDX-License-Identifier: MIT
**/

#ifndef AORUS_UI_H_
#define AORUS_UI_H_

#include "AorusBios.h"

#define MODE_EASY      1
#define MODE_ADVANCED  2

// Loop results
#define UI_CONTINUE     0
#define UI_SWITCH_MODE  10
#define UI_GOTO_FAV     11

// Layout (1920x1080)
#define HEADER_H   82
#define TABS_Y     92
#define TAB_W      280
#define TAB_H      46
#define BODY_Y     148
#define BODY_BOTTOM  930
#define HELP_Y     944
#define BUTTONS_Y  1014

// Global "redraw" requests
#define RD_ALL    0x01
#define RD_BODY   0x02
#define RD_HELP   0x04
#define RD_CLOCK  0x08
#define RD_PANEL  0x10

// GUI picture indices inside the GIGABYTE resource (see docs/ASSETS.md)
#define GUI_AORUS_LOGO      0
#define GUI_ADV_OFF         1
#define GUI_ADV_ON          2
#define GUI_EASY_OFF        4
#define GUI_EASY_ON         5
#define GUI_TAB             42
#define GUI_STAR            26
#define GUI_ICON_SEARCH     119
#define GUI_ICON_HELP       120
#define GUI_ICON_FAN        112
#define GUI_ICON_CHIP       110
#define GUI_ICON_GLOBE      109
#define GUI_ICON_REFRESH    114
#define GUI_ICON_EXIT       113

#define GUI_TAB_ICON(i)  ((i) == 1 ? 111 : (i) == 2 ? 115 : (i) == 3 ? 118 : (i) == 4 ? 117 : (i) == 5 ? 116 : 113)

extern UINT8  gUiMode;

VOID
UiInit (
  VOID
  );

VOID
UiDrawBackground (
  VOID
  );

VOID
UiRestoreBackground (
  INT32  X,
  INT32  Y,
  INT32  W,
  INT32  H
  );

VOID
UiDrawHeader (
  VOID
  );

VOID
UiDrawClock (
  BOOLEAN  Present
  );

VOID
UiDrawMonitorPanel (
  INT32    X,
  INT32    Y,
  INT32    W,
  INT32    H,
  BOOLEAN  Present
  );

VOID
UiDrawBottomButtons (
  VOID
  );

VOID
UiDrawButton (
  INT32         X,
  INT32         Y,
  INT32         W,
  INT32         H,
  UINT32        IconId,
  CONST CHAR16  *Text,
  BOOLEAN       Active
  );

VOID
UiDrawTab (
  INT32         X,
  INT32         Y,
  UINT32        IconId,
  CONST CHAR16  *Text,
  BOOLEAN       Active
  );

UINTN
UiHandleGlobalKey (
  CONST KEY  *Key,
  BOOLEAN    *Handled
  );

BOOLEAN
UiTick (
  VOID
  );

VOID
UiFormatStatementValue (
  CONST SDB_STMT  *S,
  CHAR16          *Buf,
  UINTN           BufSize
  );

BOOLEAN
UiEditStatement (
  SDB_STMT  *S,
  INT32     Direction
  );

BOOLEAN
UiEditNumericTyped (
  SDB_STMT  *S,
  CHAR16    Ch
  );

BOOLEAN
UiIsLanguage (
  CONST SDB_STMT  *S
  );

BOOLEAN
UiIsSpecial (
  CONST SDB_STMT  *S,
  UINT16          Key
  );

UINTN
UiRunAction (
  UINT8  Action
  );

UINTN
AdvancedRun (
  BOOLEAN  Favorites
  );

UINTN
EasyRun (
  VOID
  );

VOID
UiShowForm (
  UINT16  FormId
  );

#endif
