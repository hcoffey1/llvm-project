//===-- RISCVAsmPrinter.cpp - RISCV LLVM assembly writer ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the RISCV assembly language.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/RISCVInstPrinter.h"
#include "MCTargetDesc/RISCVMCExpr.h"
#include "MCTargetDesc/RISCVTargetStreamer.h"
#include "RISCV.h"
#include "RISCVSubtarget.h"
#include "RISCVTargetMachine.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <cstdio>
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

STATISTIC(RISCVNumInstrsCompressed,
          "Number of RISC-V Compressed instructions emitted");

namespace {
class RISCVAsmPrinter : public AsmPrinter {
  const MCSubtargetInfo *STI;

public:
  explicit RISCVAsmPrinter(TargetMachine &TM,
                           std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), STI(TM.getMCSubtargetInfo()) {}

  StringRef getPassName() const override { return "RISCV Assembly Printer"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void emitInstruction(const MachineInstr *MI) override;

  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &OS) override;
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &OS) override;

  void EmitToStreamer(MCStreamer &S, const MCInst &Inst);
  bool emitPseudoExpansionLowering(MCStreamer &OutStreamer,
                                   const MachineInstr *MI);

  // Wrapper needed for tblgenned pseudo lowering.
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp) const {
    return LowerRISCVMachineOperandToMCOperand(MO, MCOp, *this);
  }

  // XRay Support
  void LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr *MI);
  void LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr *MI);
  void LowerPATCHABLE_TAIL_CALL(const MachineInstr *MI);
  void LowerPATCHABLE_EVENT_CALL(const MachineInstr *MI);

  void emitStartOfAsmFile(Module &M) override;
  void emitEndOfAsmFile(Module &M) override;

private:
  void emitAttributes();
  // XRay Support
  void emitSled(const MachineInstr *MI, SledKind Kind);
};
}

#define GEN_COMPRESS_INSTR
#include "RISCVGenCompressInstEmitter.inc"
void RISCVAsmPrinter::EmitToStreamer(MCStreamer &S, const MCInst &Inst) {
  MCInst CInst;
  bool Res = compressInst(CInst, Inst, *STI, OutStreamer->getContext());
  if (Res)
    ++RISCVNumInstrsCompressed;
  AsmPrinter::EmitToStreamer(*OutStreamer, Res ? CInst : Inst);
}

// Simple pseudo-instructions have their lowering (with expansion to real
// instructions) auto-generated.
#include "RISCVGenMCPseudoLowering.inc"

void RISCVAsmPrinter::emitInstruction(const MachineInstr *MI) {
  // Do any auto-generated pseudo lowerings.
  if (emitPseudoExpansionLowering(*OutStreamer, MI))
    return;

  switch (MI->getOpcode()) {
  case TargetOpcode::PATCHABLE_FUNCTION_ENTER: {
    // This switch case section is only for handling XRay sleds.
    //
    // patchable-function-entry is handled in lowerRISCVMachineInstrToMCInst
    // Therefore, we break out of the switch statement if we encounter it here.
    const Function &F = MI->getParent()->getParent()->getFunction();
    if (F.hasFnAttribute("patchable-function-entry")) {
      break;
    }

    LowerPATCHABLE_FUNCTION_ENTER(MI);
    return;
  }
  case TargetOpcode::PATCHABLE_FUNCTION_EXIT: {
    LowerPATCHABLE_FUNCTION_EXIT(MI);
    return;
  }
  case TargetOpcode::PATCHABLE_TAIL_CALL: {
    LowerPATCHABLE_TAIL_CALL(MI);
    return;
  }
  case TargetOpcode::PATCHABLE_EVENT_CALL: {
    LowerPATCHABLE_EVENT_CALL(MI);
    return;
  }
  }

  MCInst TmpInst;
  if (!lowerRISCVMachineInstrToMCInst(MI, TmpInst, *this))
    EmitToStreamer(*OutStreamer, TmpInst);
}

bool RISCVAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &OS) {
  // First try the generic code, which knows about modifiers like 'c' and 'n'.
  if (!AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, OS))
    return false;

  const MachineOperand &MO = MI->getOperand(OpNo);
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      return true; // Unknown modifier.
    case 'z':      // Print zero register if zero, regular printing otherwise.
      if (MO.isImm() && MO.getImm() == 0) {
        OS << RISCVInstPrinter::getRegisterName(RISCV::X0);
        return false;
      }
      break;
    case 'i': // Literal 'i' if operand is not a register.
      if (!MO.isReg())
        OS << 'i';
      return false;
    }
  }

  switch (MO.getType()) {
  case MachineOperand::MO_Immediate:
    OS << MO.getImm();
    return false;
  case MachineOperand::MO_Register:
    OS << RISCVInstPrinter::getRegisterName(MO.getReg());
    return false;
  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, OS);
    return false;
  case MachineOperand::MO_BlockAddress: {
    MCSymbol *Sym = GetBlockAddressSymbol(MO.getBlockAddress());
    Sym->print(OS, MAI);
    return false;
  }
  default:
    break;
  }

  return true;
}

bool RISCVAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &OS) {
  if (!ExtraCode) {
    const MachineOperand &MO = MI->getOperand(OpNo);
    // For now, we only support register memory operands in registers and
    // assume there is no addend
    if (!MO.isReg())
      return true;

    OS << "0(" << RISCVInstPrinter::getRegisterName(MO.getReg()) << ")";
    return false;
  }

  return AsmPrinter::PrintAsmMemoryOperand(MI, OpNo, ExtraCode, OS);
}

bool RISCVAsmPrinter::runOnMachineFunction(MachineFunction &MF) {
  // Set the current MCSubtargetInfo to a copy which has the correct
  // feature bits for the current MachineFunction
  MCSubtargetInfo &NewSTI =
    OutStreamer->getContext().getSubtargetCopy(*TM.getMCSubtargetInfo());
  NewSTI.setFeatureBits(MF.getSubtarget().getFeatureBits());
  STI = &NewSTI;

  SetupMachineFunction(MF);
  emitFunctionBody();

  // Emit the XRay table
  emitXRayTable();
  return false;
}

void RISCVAsmPrinter::LowerPATCHABLE_FUNCTION_ENTER(const MachineInstr *MI) {
  emitSled(MI, SledKind::FUNCTION_ENTER);
}

void RISCVAsmPrinter::LowerPATCHABLE_FUNCTION_EXIT(const MachineInstr *MI) {
  emitSled(MI, SledKind::FUNCTION_EXIT);
}

void RISCVAsmPrinter::LowerPATCHABLE_TAIL_CALL(const MachineInstr *MI) {
  emitSled(MI, SledKind::TAIL_CALL);
}

void RISCVAsmPrinter::LowerPATCHABLE_EVENT_CALL(const MachineInstr *MI) {
	//assert(Subtarget->is64Bit() && "XRay custom events only supports RISCV64");

	// We want to emit the following pattern, which follows the x86 calling
	// convention to prepare for the trampoline call to be patched in.
	//
	//   .p2align 1, ...
	// .Lxray_event_sled_N:
	//   jmp .tmpN                     // jump across the instrumentation sled
	//   ...                           // set up arguments in register
	//   jalr __xray_CustomEvent@plt   // force dependency to symbol
	//   ...
	//   <jump here>
	//
	// After patching, it would look something like:
	//
	//   nopw (2-byte nop)
	//   ...
	//   jalr __xrayCustomEvent   // already lowered
	//   ...
	//
	// ---
	// First we emit the label and the jump.
	OutStreamer->emitCodeAlignment(4, &getSubtargetInfo());
	auto CurSled = OutContext.createTempSymbol("xray_event_sled_", true);
	OutStreamer->AddComment("# XRay Custom Event Log");
	OutStreamer->emitLabel(CurSled);
	auto Target = OutContext.createTempSymbol();

	const MCExpr *TargetExpr = MCSymbolRefExpr::create(
			      Target, MCSymbolRefExpr::VariantKind::VK_None, OutContext);

	// Emit "J .tmpN" instruction, which jumps over the sled to the actual
	// start of function.
	EmitToStreamer(
	  *OutStreamer,
	  MCInstBuilder(RISCV::JAL).addReg(RISCV::X0).addExpr(TargetExpr));

	// The default C calling convention will place two arguments into A0 and
	// A1 -- so we only work with those.
	const Register DestRegs[] = {RISCV::X10, RISCV::X11};
	bool UsedMask[] = {false, false};
	// Filled out in loop.
	Register SrcRegs[] = {0, 0};
	long int UsedCnt = 0;        // Keep track of number of values that must be pushed to stack
	long int StackPosition = 0;  // Keep track of stack position when saving to stack

	// Then we put the operands in the A0 and A1 registers. We spill the
	// values in the register before we clobber them, and mark them as used in
	// UsedMask. In case the arguments are already in the correct register, we use
	// emit nops appropriately sized to keep the sled the same size in every
	// situation.
	for (unsigned I = 0; I < MI->getNumOperands(); ++I) {
          auto Op = MI->getOperand(I);
          assert(Op.isReg() && "Only support arguments in registers");
	  SrcRegs[I] = Op.getReg();
	  if (SrcRegs[I] != DestRegs[I]) {
	    UsedMask[I] = true;
	    UsedCnt++;
	  }
	}
	
	// Increment stack counter accordingly
	// FIXME: We should add 4 or 8 depending on whether we're using riscv32 or riscv64
	EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
	                                            .addReg(RISCV::X2)
	                                            .addReg(RISCV::X2)
	                                            .addImm(-(UsedCnt * 8)));

	StackPosition = UsedCnt;
	for (unsigned I = 0; I < MI->getNumOperands(); ++I)
	  if (SrcRegs[I] != DestRegs[I]) {
	    EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::SD)
	                                                .addReg(DestRegs[I])
							.addReg(RISCV::X2)
							.addImm(((/*no of used regs*/ StackPosition) - 1) * 8));
	    StackPosition -= 1;
	  } else {
	    // Emit 2 NOPS
	    // 1 corresponsing to pushing the value to stack
            // 1 corresponding to moving the value to the destination register
	    EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0));
            EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
							.addReg(RISCV::X0)
							.addReg(RISCV::X0)
							.addImm(0));
	  }

	// Now that the register values are stashed, mov arguments into place.
	// FIXME: This doesn't work if one of the later SrcRegs is equal to an
	// earlier DestReg. We will have already overwritten over the register before
	// we can copy from it.
	for (unsigned I = 0; I < MI->getNumOperands(); ++I)
	  if (SrcRegs[I] != DestRegs[I])
	    EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
                                                       .addReg(DestRegs[I])
                                                       .addReg(SrcRegs[I])
                                                       .addImm(0));

	// We emit a hard dependency on the __xray_CustomEvent symbol, which is the
	// name of the trampoline to be implemented by the XRay runtime.
	auto TSym = OutContext.getOrCreateSymbol("__xray_CustomEvent");
	EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::JAL)
			                            .addReg(RISCV::X1)
	                                            .addExpr(MCSymbolRefExpr::create(TSym, OutContext))); //Will this work???
	// MachineOperand TOp = MachineOperand::CreateMCSymbol(TSym);
	// if (isPositionIndependent())
	//   TOp.setTargetFlags(RISCVII::MO_PLT);

	// MCOperand CustomEventOperand;
	//if(LowerRISCVMachineOperandToMCOperand(TOp, CustomEventOperand, *this)) {
	  // Emit the call instruction.
	//  EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::JAL)
	//                                              .addOperand(CustomEventOperand)); //Will this work???
	//} else { 
	//  assert("Did not lower Custom Event symbol");
	//}
	
	// Restore caller-saved and used registers.
	StackPosition = 0;  // Keep track of stack position when restoring from stack
	
	for (unsigned I = sizeof UsedMask; I-- > 0;)
	  if (UsedMask[I]) {
	    // Emit a load from stack to destregs[i].
	    EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::LD)
	                                                .addReg(DestRegs[I])
                                                	.addReg(RISCV::X2)
	                                                .addImm(StackPosition * 8));
	StackPosition += 1;
	}
	else
	  // Emit a nop
	  EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
	                                              .addReg(RISCV::X0)
	                                              .addReg(RISCV::X0)
	                                              .addImm(0));

        // Add number of UsedRegs * 8 to sp. If no registers were used, then we essentially add a nop.
	EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
	                                            .addReg(RISCV::X2)
	                                            .addReg(RISCV::X2)
	                                            .addImm(UsedCnt * 8));    // We can add a reg size check
	// It will be 4 for 32 bit, 8 for 64 bit
	
	OutStreamer->AddComment("xray custom event end.");
	
	// Record the sled version. Version 0 of this sled was spelled differently, so
	// we let the runtime handle the different offsets we're using. Version 2
	// changed the absolute address to a PC-relative address.
	OutStreamer->emitLabel(Target);
	recordSled(CurSled, *MI, SledKind::CUSTOM_EVENT, 2);
}

void RISCVAsmPrinter::emitStartOfAsmFile(Module &M) {
  if (TM.getTargetTriple().isOSBinFormatELF())
    emitAttributes();
}

void RISCVAsmPrinter::emitEndOfAsmFile(Module &M) {
  RISCVTargetStreamer &RTS =
      static_cast<RISCVTargetStreamer &>(*OutStreamer->getTargetStreamer());

  if (TM.getTargetTriple().isOSBinFormatELF())
    RTS.finishAttributeSection();
}

void RISCVAsmPrinter::emitAttributes() {
  RISCVTargetStreamer &RTS =
      static_cast<RISCVTargetStreamer &>(*OutStreamer->getTargetStreamer());
  RTS.emitTargetAttributes(*STI);
}

void RISCVAsmPrinter::emitSled(const MachineInstr *MI, SledKind Kind) {
  // The following variable holds the count of the number of NOPs to be patched
  // in for XRay instrumentation during compilation. RISCV64 needs 18 NOPs,
  // RISCV32 needs 14 NOPs.
  const uint8_t NoopsInSledCount =
      MI->getParent()->getParent()->getSubtarget<RISCVSubtarget>().is64Bit()
          ? 18
          : 14;

  // We want to emit the jump instruction and the nops constituting the sled.
  // The format is as follows:
  // .Lxray_sled_N
  //   ALIGN
  //   J .tmpN (60 or 76 byte jump, depending on ISA)
  //   14 or 18 NOP instructions
  // .tmpN

  OutStreamer->emitCodeAlignment(4, &getSubtargetInfo());
  auto CurSled = OutContext.createTempSymbol("xray_sled_", true);
  OutStreamer->emitLabel(CurSled);
  auto Target = OutContext.createTempSymbol();

  const MCExpr *TargetExpr = MCSymbolRefExpr::create(
      Target, MCSymbolRefExpr::VariantKind::VK_None, OutContext);

  // Emit "J bytes" instruction, which jumps over the nop sled to the actual
  // start of function.
  EmitToStreamer(
      *OutStreamer,
      MCInstBuilder(RISCV::JAL).addReg(RISCV::X0).addExpr(TargetExpr));

  // Emit NOP instructions
  for (int8_t I = 0; I < NoopsInSledCount; I++)
    EmitToStreamer(*OutStreamer, MCInstBuilder(RISCV::ADDI)
                                     .addReg(RISCV::X0)
                                     .addReg(RISCV::X0)
                                     .addImm(0));

  OutStreamer->emitLabel(Target);
  recordSled(CurSled, *MI, Kind, 2);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVAsmPrinter() {
  RegisterAsmPrinter<RISCVAsmPrinter> X(getTheRISCV32Target());
  RegisterAsmPrinter<RISCVAsmPrinter> Y(getTheRISCV64Target());
}
