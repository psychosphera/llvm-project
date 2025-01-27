add_llvm_component_library(LLVMPowerPCDesc
  PPCAsmBackend.cpp
  PPCInstPrinter.cpp
  PPCMCTargetDesc.cpp
  PPCMCAsmInfo.cpp
  PPCMCCodeEmitter.cpp
  PPCMCExpr.cpp
  PPCPredicates.cpp
  PPCELFObjectWriter.cpp
  PPCXCOFFObjectWriter.cpp
  PPCWinCOFFObjectWriter.cpp
  PPCELFStreamer.cpp
  PPCXCOFFStreamer.cpp
  PPCWinCOFFStreamer.cpp

  LINK_COMPONENTS
  BinaryFormat
  CodeGenTypes
  MC
  PowerPCInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  PowerPC
  )
