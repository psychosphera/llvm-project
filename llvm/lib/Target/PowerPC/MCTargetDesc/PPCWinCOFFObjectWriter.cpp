//===-- PPCXCOFFObjectWriter.cpp - PowerPC XCOFF Writer -------------------===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCFixupKinds.h"
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

namespace {
class PPCWinCOFFObjectWriter : public MCWinCOFFObjectTargetWriter {
  static constexpr uint8_t SignBitMask = 0x80;

public:
  PPCWinCOFFObjectWriter();

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsCrossSection,
                                const MCAsmBackend &MAB) const;
  bool recordRelocation(const MCFixup &) const override;
};
} // end anonymous namespace

PPCWinCOFFObjectWriter::PPCWinCOFFObjectWriter()
    : MCWinCOFFObjectTargetWriter(llvm::COFF::IMAGE_FILE_MACHINE_PPCBE) { }

std::unique_ptr<MCObjectTargetWriter>
llvm::createPPCWinCOFFObjectWriter() {
  return std::make_unique<PPCWinCOFFObjectWriter>();
}

unsigned PPCWinCOFFObjectWriter::getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsCrossSection,
                                const MCAsmBackend &MAB) const
{
  MCSymbolRefExpr::VariantKind Modifier =
    Target.isAbsolute() ? MCSymbolRefExpr::VK_None : Target.getSymA()->getKind();
  const unsigned FixupKind = Fixup.getKind();
  assert(Modifier != MCSymbolRefExpr::VK_PPC_TOC && 
         Modifier != MCSymbolRefExpr::VK_PPC_TOC_HA &&
         Modifier != MCSymbolRefExpr::VK_PPC_TOC_HI &&
         Modifier != MCSymbolRefExpr::VK_PPC_TOC_LO &&
         Modifier != MCSymbolRefExpr::VK_PPC_TOCBASE &&
         "WinCOFF doesn't use TOC for any implemented platform");
  switch(FixupKind) {
  // TODO: make sure these are all valid for the target's variant kind.
  case FK_NONE:
  case PPC::fixup_ppc_nofixup:
    return llvm::COFF::IMAGE_REL_PPC_ABSOLUTE;
  case PPC::fixup_ppc_half16ds:
    return llvm::COFF::IMAGE_REL_PPC_ADDR14;
  case FK_Data_2:
    return llvm::COFF::IMAGE_REL_PPC_ADDR16;
  case FK_Data_4:
    switch (Modifier) {
    case MCSymbolRefExpr::VK_COFF_IMGREL32:
      return COFF::IMAGE_REL_PPC_ADDR32NB;
    case MCSymbolRefExpr::VK_SECREL:
      return COFF::IMAGE_REL_PPC_SECREL;
    default:
      return COFF::IMAGE_REL_PPC_ADDR32;
    }
  case FK_Data_8:
    return llvm::COFF::IMAGE_REL_PPC_ADDR64;
  case FK_GPRel_2:
    return llvm::COFF::IMAGE_REL_PPC_GPREL;
  case FK_SecRel_2:
    return llvm::COFF::IMAGE_REL_PPC_SECREL16;
  case FK_SecRel_4:
    return llvm::COFF::IMAGE_REL_PPC_SECREL;
  case PPC::fixup_ppc_br24:
    return llvm::COFF::IMAGE_REL_PPC_REL24;
  case PPC::fixup_ppc_brcond14:
    return llvm::COFF::IMAGE_REL_PPC_REL14;
  case PPC::fixup_ppc_brcond14abs:
    return llvm::COFF::IMAGE_REL_PPC_ADDR14;
  case PPC::fixup_ppc_half16:
    switch (Modifier) {
    case MCSymbolRefExpr::VK_PPC_LO: 
      return llvm::COFF::IMAGE_REL_PPC_REFLO;
    case MCSymbolRefExpr::VK_PPC_HI:
    case MCSymbolRefExpr::VK_PPC_HA:
      return llvm::COFF::IMAGE_REL_PPC_REFHI;
    default: 
      ;
    }
    [[fallthrough]];
  // TODO: IMAGE_REL_PPC_SECRELLO, IMAGE_REL_PPC_TOKEN
  default:
    dbgs() << "FixupKind=" << Fixup.getKind() << ", VariantKind=" << Target.getSymA()->getKind() << "\n";
    llvm_unreachable("Unimplemented PPC fixup.");
  }
}

bool PPCWinCOFFObjectWriter::recordRelocation(const MCFixup &) const {
  return true; // FIXME: Not sure if valid.
}
