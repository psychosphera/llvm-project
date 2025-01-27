//===--- MSVC.h - MSVC ToolChain Implementations ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_XBOX360_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_XBOX360_H

#include "AMDGPU.h"
#include "Cuda.h"
#include "LazyDetector.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/ToolChain.h"
#include "llvm/Frontend/Debug/Options.h"
#include "llvm/WindowsDriver/MSVCPaths.h"

namespace clang {
namespace driver {
namespace tools {

/// Visual studio tools.
namespace xedk {
class LLVM_LIBRARY_VISIBILITY Linker final : public Tool {
public:
  Linker(const ToolChain &TC) : Tool("xedk::Linker", "linker", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return true; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};

/*
class LLVM_LIBRARY_VISIBILITY ImageXeX final : public Tool {
public:
  ImageXeX(const ToolChain &TC) : Tool("xedk::ImageXeX", "imagexex", TC) {}

  bool hasIntegratedCPP() const override { return false; }
  bool isLinkJob() const override { return false; }

  void ConstructJob(Compilation &C, const JobAction &JA,
                    const InputInfo &Output, const InputInfoList &Inputs,
                    const llvm::opt::ArgList &TCArgs,
                    const char *LinkingOutput) const override;
};
*/

} // end namespace xedk

} // end namespace tools

namespace toolchains {

class LLVM_LIBRARY_VISIBILITY Xbox360ToolChain : public ToolChain {
public:
  Xbox360ToolChain(const Driver &D, const llvm::Triple &Triple,
                const llvm::opt::ArgList &Args);

  llvm::opt::DerivedArgList *
  TranslateArgs(const llvm::opt::DerivedArgList &Args, StringRef BoundArch,
                Action::OffloadKind DeviceOffloadKind) const override;

  UnwindTableLevel
  getDefaultUnwindTableLevel(const llvm::opt::ArgList &Args) const override;
  bool isPICDefault() const override;
  bool isPIEDefault(const llvm::opt::ArgList &Args) const override;
  bool isPICDefaultForced() const override;

  /// Set CodeView as the default debug info format for non-MachO binary
  /// formats, and to DWARF otherwise. Users can use -gcodeview and -gdwarf to
  /// override the default.
  llvm::codegenoptions::DebugInfoFormat getDefaultDebugFormat() const override {
    return getTriple().isOSBinFormatMachO()
               ? llvm::codegenoptions::DIF_DWARF
               : llvm::codegenoptions::DIF_CodeView;
  }

  /// Set the debugger tuning to "default", since we're definitely not tuning
  /// for GDB.
  llvm::DebuggerKind getDefaultDebuggerTuning() const override {
    return llvm::DebuggerKind::Default;
  }

  unsigned GetDefaultDwarfVersion() const override {
    return 4;
  }

  std::string getSubDirectoryPath(llvm::SubDirectoryType Type, llvm::StringRef SubdirParent = "") const;

  std::string ComputeEffectiveClangTriple(const llvm::opt::ArgList &Args,
                                          types::ID InputType) const override;

  bool FoundMSVCInstall() const { return !XEDKRoot.empty(); }

protected:
  void AddSystemIncludeWithSubfolder(const llvm::opt::ArgList &DriverArgs,
                                     llvm::opt::ArgStringList &CC1Args,
                                     const std::string &folder,
                                     const Twine &subfolder1,
                                     const Twine &subfolder2 = "",
                                     const Twine &subfolder3 = "") const;

  Tool *buildLinker() const override;
private:
  std::string XEDKRoot;

  Tool *buildImageXeX() const;
};

} // end namespace toolchains
} // end namespace driver
} // end namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_MSVC_H
