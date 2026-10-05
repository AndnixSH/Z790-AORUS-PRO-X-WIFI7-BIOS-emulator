/** @file
  Access to the setup database and image pack built from the BIOS image.

  SPDX-License-Identifier: MIT
**/

#include "AorusBios.h"

SDB_CONTEXT  gSdb;

STATIC IMAGE      *mImages;
STATIC PAK_ENTRY  *mPakEntries;
STATIC UINT32     mImageCount;

STATIC
VOID *
FindSection (
  SDB_HEADER  *Hdr,
  CONST CHAR8 *Tag,
  UINT32      *Count
  )
{
  SDB_SECTION  *Sec;
  UINT32       Index;

  Sec = (SDB_SECTION *)(Hdr + 1);
  for (Index = 0; Index < Hdr->SectionCount; Index++) {
    if (CompareMem (Sec[Index].Tag, Tag, 4) == 0) {
      if (Count != NULL) {
        *Count = Sec[Index].Count;
      }

      return (UINT8 *)Hdr + Sec[Index].Offset;
    }
  }

  if (Count != NULL) {
    *Count = 0;
  }

  return NULL;
}

EFI_STATUS
DataLoad (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Sdb;
  UINTN       SdbSize;
  VOID        *Pak;
  UINTN       PakSize;
  VOID        *Font;
  UINTN       FontSize;
  SDB_HEADER  *Hdr;
  PAK_HEADER  *PakHdr;
  UINT32      Index;
  UINT32      Dummy;

  Status = GetSectionFromAnyFv (&gAorusSdbFileGuid, EFI_SECTION_RAW, 0, &Sdb, &SdbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Hdr = Sdb;
  if ((CompareMem (Hdr->Magic, "AORUSSDB", 8) != 0) || (Hdr->Version != SDB_VERSION)) {
    return EFI_INCOMPATIBLE_VERSION;
  }

  gSdb.Base        = Sdb;
  gSdb.Info        = FindSection (Hdr, "INFO", &Dummy);
  gSdb.LangBase    = FindSection (Hdr, "LANG", &gSdb.LangCount);
  gSdb.Langs       = (SDB_LANG *)gSdb.LangBase;
  gSdb.StrData     = FindSection (Hdr, "STRD", &Dummy);
  gSdb.Vars        = FindSection (Hdr, "VARS", &gSdb.VarCount);
  gSdb.VarDefaults = FindSection (Hdr, "VDAT", &Dummy);
  gSdb.Forms       = FindSection (Hdr, "FORM", &gSdb.FormCount);
  gSdb.Stmts       = FindSection (Hdr, "STMT", &gSdb.StmtCount);
  gSdb.Opts        = FindSection (Hdr, "OPTS", &gSdb.OptCount);
  gSdb.Expr        = FindSection (Hdr, "EXPR", &Dummy);
  gSdb.Tabs        = FindSection (Hdr, "TABS", &gSdb.TabCount);
  gSdb.Specs       = FindSection (Hdr, "SPEC", &gSdb.SpecCount);
  gSdb.Named       = FindSection (Hdr, "NSTR", &gSdb.NamedCount);

  gSdb.EnLang = 0;
  for (Index = 0; Index < gSdb.LangCount; Index++) {
    if (AsciiStrCmp (gSdb.Langs[Index].Code, "en-US") == 0) {
      gSdb.EnLang = Index;
    }
  }

  gSdb.Lang = gSdb.EnLang;

  gSdb.Cur  = AllocateZeroPool (gSdb.VarCount * sizeof (UINT8 *));
  gSdb.Prev = AllocateZeroPool (gSdb.VarCount * sizeof (UINT8 *));
  for (Index = 0; Index < gSdb.VarCount; Index++) {
    gSdb.Cur[Index]  = AllocateCopyPool (gSdb.Vars[Index].Size, gSdb.VarDefaults + gSdb.Vars[Index].DataOffset);
    gSdb.Prev[Index] = AllocateCopyPool (gSdb.Vars[Index].Size, gSdb.Cur[Index]);
  }

  //
  // Pictures
  //
  Status = GetSectionFromAnyFv (&gAorusGuiFileGuid, EFI_SECTION_RAW, 0, &Pak, &PakSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  PakHdr = Pak;
  if (CompareMem (PakHdr->Magic, "AORUSPAK", 8) != 0) {
    return EFI_INCOMPATIBLE_VERSION;
  }

  mImageCount = PakHdr->Count;
  mPakEntries = (PAK_ENTRY *)(PakHdr + 1);
  mImages     = AllocateZeroPool (mImageCount * sizeof (IMAGE));
  for (Index = 0; Index < mImageCount; Index++) {
    mImages[Index].Width  = mPakEntries[Index].Width;
    mImages[Index].Height = mPakEntries[Index].Height;
    mImages[Index].Pixels = (UINT32 *)((UINT8 *)Pak + mPakEntries[Index].Offset);
  }

  //
  // Font
  //
  Status = GetSectionFromAnyFv (&gAorusFontFileGuid, EFI_SECTION_RAW, 0, &Font, &FontSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return FontInit (Font, FontSize);
}

CONST IMAGE *
DataImage (
  UINT32  Id
  )
{
  UINT32  Index;

  for (Index = 0; Index < mImageCount; Index++) {
    if (mPakEntries[Index].Id == Id) {
      return &mImages[Index];
    }
  }

  return NULL;
}

STATIC
CONST CHAR16 *
LangStr (
  UINT32  Lang,
  UINT16  Id
  )
{
  SDB_LANG  *L;
  UINT32    *Table;

  L = &gSdb.Langs[Lang];
  if (Id > L->MaxId) {
    return NULL;
  }

  Table = (UINT32 *)(gSdb.LangBase + L->TableOffset);
  if (Table[Id] == 0) {
    return NULL;
  }

  return (CONST CHAR16 *)(gSdb.StrData + Table[Id] - 1);
}

CONST CHAR16 *
SdbStr (
  UINT16  Id
  )
{
  CONST CHAR16  *S;

  if (Id == 0) {
    return L"";
  }

  S = LangStr (gSdb.Lang, Id);
  if ((S == NULL) || (*S == 0)) {
    S = LangStr (gSdb.EnLang, Id);
  }

  return S != NULL ? S : L"";
}

CONST CHAR16 *
SdbNamed (
  UINT32  NamedId
  )
{
  if (NamedId >= gSdb.NamedCount) {
    return L"";
  }

  return SdbStr (gSdb.Named[NamedId]);
}

SDB_FORM *
SdbFindForm (
  UINT16  FormId
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.FormCount; Index++) {
    if (gSdb.Forms[Index].FormId == FormId) {
      return &gSdb.Forms[Index];
    }
  }

  return NULL;
}

SDB_STMT *
SdbSpecial (
  UINT16  Key
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.SpecCount; Index++) {
    if (gSdb.Specs[Index].Key == Key) {
      return &gSdb.Stmts[gSdb.Specs[Index].Stmt];
    }
  }

  return NULL;
}

UINT64
SdbSpecialValue (
  UINT16  Key,
  UINT64  Fallback
  )
{
  SDB_STMT  *S;

  S = SdbSpecial (Key);
  if ((S == NULL) || (S->VarIndex == SDB_NO_VAR)) {
    return Fallback;
  }

  return SdbGetValue (S);
}

VOID
SdbSetSpecialValue (
  UINT16  Key,
  UINT64  Value
  )
{
  SDB_STMT  *S;

  S = SdbSpecial (Key);
  if ((S != NULL) && (S->VarIndex != SDB_NO_VAR)) {
    SdbSetValue (S, Value);
  }
}

STATIC
UINT64
LoadVar (
  UINT16  Var,
  UINT16  Offset,
  UINT8   Width
  )
{
  UINT64  Value;

  if ((Var >= gSdb.VarCount) || ((UINT32)Offset + Width > gSdb.Vars[Var].Size)) {
    return 0;
  }

  Value = 0;
  CopyMem (&Value, gSdb.Cur[Var] + Offset, MIN (Width, sizeof (Value)));
  return Value;
}

UINT64
SdbEval (
  UINT32  Offset
  )
{
  UINT64  Stack[32];
  UINTN   Sp;
  UINT8   *P;
  UINT64  A;
  UINT64  B;
  UINT64  C;
  UINT16  Count;
  UINT16  Index;

  if (Offset == SDB_NO_EXPR) {
    return 0;
  }

  Sp = 0;
  P  = gSdb.Expr + Offset;
  for ( ; ;) {
    UINT8  Op = *P++;

    switch (Op) {
      case SDB_E_END:
        return Sp > 0 ? Stack[Sp - 1] : 0;

      case SDB_E_PUSH:
        CopyMem (&A, P, 8);
        P += 8;
        if (Sp < ARRAY_SIZE (Stack)) {
          Stack[Sp++] = A;
        }

        continue;

      case SDB_E_LOAD:
        A  = LoadVar (*(UINT16 *)P, *(UINT16 *)(P + 2), P[4]);
        P += 5;
        if (Sp < ARRAY_SIZE (Stack)) {
          Stack[Sp++] = A;
        }

        continue;

      case SDB_E_INLIST:
        Count = *(UINT16 *)P;
        P    += 2;
        A     = Sp > 0 ? Stack[Sp - 1] : 0;
        B     = 0;
        for (Index = 0; Index < Count; Index++) {
          if (A == *(UINT16 *)(P + 2 * Index)) {
            B = 1;
          }
        }

        P += 2 * Count;
        if (Sp > 0) {
          Stack[Sp - 1] = B;
        }

        continue;

      case SDB_E_NOT:
        if (Sp > 0) {
          Stack[Sp - 1] = Stack[Sp - 1] == 0;
        }

        continue;

      case SDB_E_BNOT:
        if (Sp > 0) {
          Stack[Sp - 1] = ~Stack[Sp - 1];
        }

        continue;

      case SDB_E_COND:
        if (Sp < 3) {
          return 0;
        }

        C           = Stack[--Sp];          // third pushed: value if true
        B           = Stack[--Sp];          // second pushed: value if false
        A           = Stack[Sp - 1];        // condition
        Stack[Sp - 1] = A ? C : B;
        continue;

      default:
        break;
    }

    //
    // Binary operators
    //
    if (Sp < 2) {
      return 0;
    }

    B = Stack[--Sp];
    A = Stack[Sp - 1];
    switch (Op) {
      case SDB_E_EQ:   A = A == B;
        break;
      case SDB_E_NE:   A = A != B;
        break;
      case SDB_E_LT:   A = A < B;
        break;
      case SDB_E_LE:   A = A <= B;
        break;
      case SDB_E_GT:   A = A > B;
        break;
      case SDB_E_GE:   A = A >= B;
        break;
      case SDB_E_AND:  A = (A != 0) && (B != 0);
        break;
      case SDB_E_OR:   A = (A != 0) || (B != 0);
        break;
      case SDB_E_BAND: A = A & B;
        break;
      case SDB_E_BOR:  A = A | B;
        break;
      case SDB_E_ADD:  A = A + B;
        break;
      case SDB_E_SUB:  A = A - B;
        break;
      case SDB_E_MUL:  A = MultU64x64 (A, B);
        break;
      case SDB_E_DIV:  A = B ? DivU64x64Remainder (A, B, NULL) : 0;
        break;
      case SDB_E_MOD:
        C = 0;
        if (B != 0) {
          DivU64x64Remainder (A, B, &C);
        }

        A = C;
        break;
      case SDB_E_SHL:  A = LShiftU64 (A, (UINTN)B);
        break;
      case SDB_E_SHR:  A = RShiftU64 (A, (UINTN)B);
        break;
      default:
        return 0;
    }

    Stack[Sp - 1] = A;
  }
}

BOOLEAN
SdbSuppressed (
  CONST SDB_STMT  *S
  )
{
  return S->Suppress != SDB_NO_EXPR && SdbEval (S->Suppress) != 0;
}

BOOLEAN
SdbGrayed (
  CONST SDB_STMT  *S
  )
{
  if ((S->QFlags & SDB_QF_READ_ONLY) != 0) {
    return TRUE;
  }

  return S->GrayOut != SDB_NO_EXPR && SdbEval (S->GrayOut) != 0;
}

BOOLEAN
SdbOptionVisible (
  CONST SDB_OPT  *O
  )
{
  return O->Suppress == SDB_NO_EXPR || SdbEval (O->Suppress) == 0;
}

UINT64
SdbGetValue (
  CONST SDB_STMT  *S
  )
{
  if ((S->VarIndex == SDB_NO_VAR) || (S->Width == 0)) {
    return S->Default;
  }

  return LoadVar (S->VarIndex, S->VarOffset, S->Width);
}

VOID
SdbSetValue (
  CONST SDB_STMT  *S,
  UINT64          Value
  )
{
  if ((S->VarIndex == SDB_NO_VAR) || (S->Width == 0) ||
      ((UINT32)S->VarOffset + S->Width > gSdb.Vars[S->VarIndex].Size))
  {
    return;
  }

  CopyMem (gSdb.Cur[S->VarIndex] + S->VarOffset, &Value, S->Width);
}

BOOLEAN
SdbChanged (
  CONST SDB_STMT  *S
  )
{
  if ((S->VarIndex == SDB_NO_VAR) || (S->Width == 0)) {
    return FALSE;
  }

  return CompareMem (gSdb.Cur[S->VarIndex] + S->VarOffset, gSdb.Prev[S->VarIndex] + S->VarOffset, S->Width) != 0;
}

CONST SDB_OPT *
SdbFindOption (
  CONST SDB_STMT  *S,
  UINT64          Value
  )
{
  UINT32  Index;

  for (Index = 0; Index < S->OptCount; Index++) {
    CONST SDB_OPT  *O = &gSdb.Opts[S->OptFirst + Index];
    if (O->Value == Value) {
      return O;
    }
  }

  return NULL;
}

SDB_STMT *
SdbStmtByQuestion (
  UINT16  QuestionId
  )
{
  UINT32  Index;

  for (Index = 0; Index < gSdb.StmtCount; Index++) {
    if ((gSdb.Stmts[Index].QuestionId == QuestionId) && ((gSdb.Stmts[Index].Flags & SDB_SF_VIRTUAL) == 0)) {
      return &gSdb.Stmts[Index];
    }
  }

  return NULL;
}

VOID
SdbFormatValue (
  CONST SDB_STMT  *S,
  CHAR16          *Buf,
  UINTN           BufSize
  )
{
  UINT64         Value;
  CONST SDB_OPT  *O;
  INT64          Signed;

  Buf[0] = 0;
  Value  = SdbGetValue (S);
  switch (S->Kind) {
    case SDB_K_ONEOF:
      O = SdbFindOption (S, Value);
      if (O != NULL) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), SdbStr (O->Text));
      } else {
        UnicodeSPrint (Buf, BufSize, L"%Ld", Value);
      }

      break;

    case SDB_K_CHECKBOX:
      StrCpyS (Buf, BufSize / sizeof (CHAR16), SdbNamed (Value ? SDB_STR_ENABLED : SDB_STR_DISABLED));
      break;

    case SDB_K_NUMERIC:
      if ((S->NumFmt >= SDB_NF_AUTO) && (Value == 0)) {
        StrCpyS (Buf, BufSize / sizeof (CHAR16), SdbNamed (SDB_STR_AUTO));
        break;
      }

      switch (S->NumFmt) {
        case SDB_NF_HEX:
          UnicodeSPrint (Buf, BufSize, L"0x%Lx", Value);
          break;
        case SDB_NF_AUTO_MV:
          UnicodeSPrint (Buf, BufSize, L"%d.%03dV", (UINT32)Value / 1000, (UINT32)Value % 1000);
          break;
        case SDB_NF_AUTO_MV_OFS:
          Signed = (S->Width == 2) ? (INT16)Value : (S->Width == 1 ? (INT8)Value : (INT32)Value);
          UnicodeSPrint (
            Buf,
            BufSize,
            L"%c%d.%03dV",
            Signed < 0 ? L'-' : L'+',
            (UINT32)(Signed < 0 ? -Signed : Signed) / 1000,
            (UINT32)(Signed < 0 ? -Signed : Signed) % 1000
            );
          break;
        case SDB_NF_AUTO_CLK:
          UnicodeSPrint (Buf, BufSize, L"%d.%02dMHz", (UINT32)Value / 100, (UINT32)Value % 100);
          break;
        default:
          UnicodeSPrint (Buf, BufSize, L"%Ld", Value);
          break;
      }

      break;

    default:
      break;
  }
}
