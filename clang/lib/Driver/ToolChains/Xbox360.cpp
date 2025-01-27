//===-- MSVC.cpp - MSVC ToolChain Implementations -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Xbox360.h"
#include "CommonArgs.h"
#include "Darwin.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Config/config.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Driver/SanitizerArgs.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"
#include <cstdio>

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

static bool canExecute(llvm::vfs::FileSystem &VFS, StringRef Path) {
  auto Status = VFS.status(Path);
  if (!Status)
    return false;
  return (Status->getPermissions() & llvm::sys::fs::perms::all_exe) != 0;
}

// Try to find Exe from XEDK.
static std::string FindXEDKExecutable(const ToolChain &TC,
                                      const char *Exe) {
  const auto &X360 = static_cast<const toolchains::Xbox360ToolChain &>(TC);
  SmallString<128> FilePath(
      X360.getSubDirectoryPath(llvm::SubDirectoryType::Bin));
  llvm::sys::path::append(FilePath, Exe);
  return std::string(canExecute(TC.getVFS(), FilePath) ? FilePath.str() : Exe);
}

void xedk::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  auto &TC = static_cast<const toolchains::Xbox360ToolChain &>(getToolChain());

  assert((Output.isFilename() || Output.isNothing()) && "invalid output");
  if (Output.isFilename())
    CmdArgs.push_back(
        Args.MakeArgString(std::string("-out:") + Output.getFilename()));

  if (!llvm::sys::Process::GetEnv("LIB")) {
    CmdArgs.push_back(Args.MakeArgString(
        Twine("-libpath:") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib)));
    CmdArgs.push_back(Args.MakeArgString(
        Twine("-libpath:") +
        TC.getSubDirectoryPath(llvm::SubDirectoryType::Lib, "atlmfc")));
  }

  if (!C.getDriver().IsCLMode() && Args.hasArg(options::OPT_L))
    for (const auto &LibPath : Args.getAllArgValues(options::OPT_L))
      CmdArgs.push_back(Args.MakeArgString("-libpath:" + LibPath));

  // Add the compiler-rt library directories to libpath if they exist to help
  // the linker find the various sanitizer, builtin, and profiling runtimes.
  for (const auto &LibPath : TC.getLibraryPaths()) {
    if (TC.getVFS().exists(LibPath))
      CmdArgs.push_back(Args.MakeArgString("-libpath:" + LibPath));
  }
  auto CRTPath = TC.getCompilerRTPath();
  if (TC.getVFS().exists(CRTPath))
    CmdArgs.push_back(Args.MakeArgString("-libpath:" + CRTPath));

  CmdArgs.push_back("-nologo");

  if (Args.hasArg(options::OPT_g_Group, options::OPT__SLASH_Z7))
    CmdArgs.push_back("-debug");

  Args.AddAllArgValues(CmdArgs, options::OPT__SLASH_link);
  
  StringRef Linker = "link";

  // Add filenames, libraries, and other linker inputs.
  for (const auto &Input : Inputs) {
    if (Input.isFilename()) {
      CmdArgs.push_back(Input.getFilename());
      continue;
    }

    const Arg &A = Input.getInputArg();

    // Render -l options differently for the MSVC linker.
    if (A.getOption().matches(options::OPT_l)) {
      StringRef Lib = A.getValue();
      const char *LinkLibArg;
      if (Lib.ends_with(".lib"))
        LinkLibArg = Args.MakeArgString(Lib);
      else
        LinkLibArg = Args.MakeArgString(Lib + ".lib");
      CmdArgs.push_back(LinkLibArg);
      continue;
    }

    // Otherwise, this is some other kind of linker input option like -Wl, -z,
    // or -L. Render it, even if MSVC doesn't understand it.
    A.renderAsInput(Args, CmdArgs);
  }

  TC.addProfileRTLibs(Args, CmdArgs);

  std::vector<const char *> Environment;

  // We need to special case some linker paths. In the case of the regular msvc
  // linker, we need to use a special search algorithm.
  llvm::SmallString<128> linkPath;
  if (Linker.equals_insensitive("link")) {
    // If we're using the MSVC linker, it's not sufficient to just use link
    // from the program PATH, because other environments like GnuWin32 install
    // their own link.exe which may come first.
    linkPath = FindXEDKExecutable(TC, "link.exe");

    if (!TC.FoundMSVCInstall() && !canExecute(TC.getVFS(), linkPath)) {
      llvm::SmallString<128> ClPath;
      ClPath = TC.GetProgramPath("cl.exe");
      if (canExecute(TC.getVFS(), ClPath)) {
        linkPath = llvm::sys::path::parent_path(ClPath);
        llvm::sys::path::append(linkPath, "link.exe");
        if (!canExecute(TC.getVFS(), linkPath))
          C.getDriver().Diag(clang::diag::warn_drv_msvc_not_found);
      } else {
        C.getDriver().Diag(clang::diag::warn_drv_msvc_not_found);
      }
    }

    // Clang handles passing the proper asan libs to the linker, which goes
    // against link.exe's /INFERASANLIBS which automatically finds asan libs.
    if (TC.getSanitizerArgs(Args).needsAsanRt())
      CmdArgs.push_back("/INFERASANLIBS:NO");
  } else {
    linkPath = TC.GetProgramPath(Linker.str().c_str());
  }

  CmdArgs.push_back("/XEX:NO");

  auto LinkCmd = std::make_unique<Command>(
      JA, *this, ResponseFileSupport::AtFileUTF16(),
      Args.MakeArgString(linkPath), CmdArgs, Inputs, Output);
  if (!Environment.empty())
    LinkCmd->setEnvironment(Environment);
  C.addCommand(std::move(LinkCmd));
}

Xbox360ToolChain::Xbox360ToolChain(const Driver &D, const llvm::Triple &Triple,
                             const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back(getDriver().Dir);

  llvm::ToolsetLayout VSLayout = llvm::ToolsetLayout::OlderVS;
  //if (Arg *A = Args.getLastArg(options::OPT__SLASH_xedkdir))
  //  XEDKRoot = A->getValue();

  // Check the command line first, that's the user explicitly telling us what to
  // use. Check the environment next, in case we're being invoked from a VS
  // command prompt. Failing that, just try to find the newest Visual Studio
  // version we can and use its default VC toolchain.
  llvm::findXEDKToolChainViaEnvironment(getVFS(), XEDKRoot,
                                          VSLayout);
}

Tool *Xbox360ToolChain::buildLinker() const {
  return new tools::xedk::Linker(*this);
}

//Tool *Xbox360ToolChain::buildImageXeX() const {
//  return new tools::xedk::ImageXeX(*this);
//}

ToolChain::UnwindTableLevel
Xbox360ToolChain::getDefaultUnwindTableLevel(const ArgList &Args) const {
  // Don't emit unwind tables by default for MachO targets.
  if (getTriple().isOSBinFormatMachO())
    return UnwindTableLevel::None;

  // All non-x86_32 Windows targets require unwind tables. However, LLVM
  // doesn't know how to generate them for all targets, so only enable
  // the ones that are actually implemented.
  if (getArch() == llvm::Triple::x86_64 || getArch() == llvm::Triple::arm ||
      getArch() == llvm::Triple::thumb || getArch() == llvm::Triple::aarch64)
    return UnwindTableLevel::Asynchronous;

  return UnwindTableLevel::None;
}

bool Xbox360ToolChain::isPICDefault() const {
  return true;
}

bool Xbox360ToolChain::isPIEDefault(const llvm::opt::ArgList &Args) const {
  return false;
}

bool Xbox360ToolChain::isPICDefaultForced() const {
  return true;
}

std::string
Xbox360ToolChain::getSubDirectoryPath(llvm::SubDirectoryType Type, llvm::StringRef SubdirParent) const {
  SmallString<8> Subdir;
  switch (Type) {
    case llvm::SubDirectoryType::Bin: 
      Subdir = "bin";
      break;
    case llvm::SubDirectoryType::Lib:
      Subdir = "lib";
      break;
    case llvm::SubDirectoryType::Include:
      Subdir = "include";
      break;
    default:
      llvm_unreachable("invalid SubDirectoryType.");
  }
  SmallString<256> Path(XEDKRoot);
  llvm::sys::path::append(Path, Subdir, "xbox", SubdirParent);
  return std::string(Path);
}

void Xbox360ToolChain::AddSystemIncludeWithSubfolder(
    const ArgList &DriverArgs, ArgStringList &CC1Args,
    const std::string &folder, const Twine &subfolder1, const Twine &subfolder2,
    const Twine &subfolder3) const {
  llvm::SmallString<128> path(folder);
  llvm::sys::path::append(path, subfolder1, subfolder2, subfolder3);
  addSystemInclude(DriverArgs, CC1Args, path);
}

std::string
Xbox360ToolChain::ComputeEffectiveClangTriple(const ArgList &Args,
                                           types::ID InputType) const {
  // For the rest of the triple, however, a computed architecture name may
  // be needed.
  llvm::Triple Triple(ToolChain::ComputeEffectiveClangTriple(Args, InputType));
  return Triple.getTriple();
}

static void TranslateOptArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                            bool SupportsForcingFramePointer,
                            const char *ExpandChar, const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT__SLASH_O));

  StringRef OptStr = A->getValue();
  for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
    const char &OptChar = *(OptStr.data() + I);
    switch (OptChar) {
    default:
      break;
    case '1':
    case '2':
    case 'x':
    case 'd':
      // Ignore /O[12xd] flags that aren't the last one on the command line.
      // Only the last one gets expanded.
      if (&OptChar != ExpandChar) {
        A->claim();
        break;
      }
      if (OptChar == 'd') {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_O0));
      } else {
        if (OptChar == '1') {
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
        } else if (OptChar == '2' || OptChar == 'x') {
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
          DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "2");
        }
        if (SupportsForcingFramePointer &&
            !DAL.hasArgNoClaim(options::OPT_fno_omit_frame_pointer))
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fomit_frame_pointer));
        if (OptChar == '1' || OptChar == '2')
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_ffunction_sections));
      }
      break;
    case 'b':
      if (I + 1 != E && isdigit(OptStr[I + 1])) {
        switch (OptStr[I + 1]) {
        case '0':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_inline));
          break;
        case '1':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_finline_hint_functions));
          break;
        case '2':
          DAL.AddFlagArg(A, Opts.getOption(options::OPT_finline_functions));
          break;
        }
        ++I;
      }
      break;
    case 'g':
      A->claim();
      break;
    case 'i':
      if (I + 1 != E && OptStr[I + 1] == '-') {
        ++I;
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_builtin));
      } else {
        DAL.AddFlagArg(A, Opts.getOption(options::OPT_fbuiltin));
      }
      break;
    case 's':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "s");
      break;
    case 't':
      DAL.AddJoinedArg(A, Opts.getOption(options::OPT_O), "2");
      break;
    case 'y': {
      bool OmitFramePointer = true;
      if (I + 1 != E && OptStr[I + 1] == '-') {
        OmitFramePointer = false;
        ++I;
      }
      if (SupportsForcingFramePointer) {
        if (OmitFramePointer)
          DAL.AddFlagArg(A,
                         Opts.getOption(options::OPT_fomit_frame_pointer));
        else
          DAL.AddFlagArg(
              A, Opts.getOption(options::OPT_fno_omit_frame_pointer));
      } else {
        // Don't warn about /Oy- in x86-64 builds (where
        // SupportsForcingFramePointer is false).  The flag having no effect
        // there is a compiler-internal optimization, and people shouldn't have
        // to special-case their build files for x86-64 clang-cl.
        A->claim();
      }
      break;
    }
    }
  }
}

static void TranslateDArg(Arg *A, llvm::opt::DerivedArgList &DAL,
                          const OptTable &Opts) {
  assert(A->getOption().matches(options::OPT_D));

  StringRef Val = A->getValue();
  size_t Hash = Val.find('#');
  if (Hash == StringRef::npos || Hash > Val.find('=')) {
    DAL.append(A);
    return;
  }

  std::string NewVal = std::string(Val);
  NewVal[Hash] = '=';
  DAL.AddJoinedArg(A, Opts.getOption(options::OPT_D), NewVal);
}

static void TranslatePermissive(Arg *A, llvm::opt::DerivedArgList &DAL,
                                const OptTable &Opts) {
  DAL.AddFlagArg(A, Opts.getOption(options::OPT__SLASH_Zc_twoPhase_));
  DAL.AddFlagArg(A, Opts.getOption(options::OPT_fno_operator_names));
}

static void TranslatePermissiveMinus(Arg *A, llvm::opt::DerivedArgList &DAL,
                                     const OptTable &Opts) {
  DAL.AddFlagArg(A, Opts.getOption(options::OPT__SLASH_Zc_twoPhase));
  DAL.AddFlagArg(A, Opts.getOption(options::OPT_foperator_names));
}

llvm::opt::DerivedArgList *
Xbox360ToolChain::TranslateArgs(const llvm::opt::DerivedArgList &Args,
                             StringRef BoundArch,
                             Action::OffloadKind OFK) const {
  DerivedArgList *DAL = new DerivedArgList(Args.getBaseArgs());
  const OptTable &Opts = getDriver().getOpts();

  // /Oy and /Oy- don't have an effect on X86-64
  bool SupportsForcingFramePointer = getArch() != llvm::Triple::x86_64;

  // The -O[12xd] flag actually expands to several flags.  We must desugar the
  // flags so that options embedded can be negated.  For example, the '-O2' flag
  // enables '-Oy'.  Expanding '-O2' into its constituent flags allows us to
  // correctly handle '-O2 -Oy-' where the trailing '-Oy-' disables a single
  // aspect of '-O2'.
  //
  // Note that this expansion logic only applies to the *last* of '[12xd]'.

  // First step is to search for the character we'd like to expand.
  const char *ExpandChar = nullptr;
  for (Arg *A : Args.filtered(options::OPT__SLASH_O)) {
    StringRef OptStr = A->getValue();
    for (size_t I = 0, E = OptStr.size(); I != E; ++I) {
      char OptChar = OptStr[I];
      char PrevChar = I > 0 ? OptStr[I - 1] : '0';
      if (PrevChar == 'b') {
        // OptChar does not expand; it's an argument to the previous char.
        continue;
      }
      if (OptChar == '1' || OptChar == '2' || OptChar == 'x' || OptChar == 'd')
        ExpandChar = OptStr.data() + I;
    }
  }

  for (Arg *A : Args) {
    if (A->getOption().matches(options::OPT__SLASH_O)) {
      // The -O flag actually takes an amalgam of other options.  For example,
      // '/Ogyb2' is equivalent to '/Og' '/Oy' '/Ob2'.
      TranslateOptArg(A, *DAL, SupportsForcingFramePointer, ExpandChar, Opts);
    } else if (A->getOption().matches(options::OPT_D)) {
      // Translate -Dfoo#bar into -Dfoo=bar.
      TranslateDArg(A, *DAL, Opts);
    } else if (A->getOption().matches(options::OPT__SLASH_permissive)) {
      // Expand /permissive
      TranslatePermissive(A, *DAL, Opts);
    } else if (A->getOption().matches(options::OPT__SLASH_permissive_)) {
      // Expand /permissive-
      TranslatePermissiveMinus(A, *DAL, Opts);
    } else if (OFK != Action::OFK_HIP) {
      // HIP Toolchain translates input args by itself.
      DAL->append(A);
    }
  }

  return DAL;
}