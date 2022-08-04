#include "X86.h"
#include "X86InstrInfo.h"

#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define X86_MACHINEINSTR_CUSTOM_PASS_NAME "Custom X86 pass"

namespace {

class X86CustomPass : public MachineFunctionPass {
public:
  static char ID;

  X86CustomPass() : MachineFunctionPass(ID) {
    initializeX86CustomPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return X86_MACHINEINSTR_CUSTOM_PASS_NAME;
  }
};

char X86CustomPass::ID = 0;

bool X86CustomPass::runOnMachineFunction(MachineFunction &MF) {
  outs() << "In " << MF.getFunction().getName() << " Function\n";

  MachineRegisterInfo(*MF);

  for (auto &MBB : MF) {
    outs() << MBB << "\n";

      const TargetInstrInfo *XII = MF.getSubtarget().getInstrInfo();
      DebugLoc DL;

    for (auto &MI : MBB) {
      if (MI.isInlineAsm()) {
        outs() << "V Found inline assembly V\n";
        outs() << MI << "\n";
        //SmallVector<MachineOperand, 8> Ops;

        //// getRegForValue();

        //Ops.push_back(MachineOperand::CreateReg(llvm::Register(-2), true));
        //Ops.push_back(MachineOperand::CreateReg(llvm::Register(-2), true));
        //// Ops.push_back(MachineOperand::CreateReg(getRegForValue(0,false));
        //// Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
        ////										/*isDef=*/false));
        //MachineInstrBuilder MIB =
        //    BuildMI(MBB, MI, DL, XII->get(TargetOpcode::PATCHABLE_EVENT_CALL));
        //for (auto &MO : Ops)
        //  MIB.add(MO);
      }

      if(MI.getOpcode() == TargetOpcode::PATCHABLE_EVENT_CALL)
      {
          outs() << "V Found custom event V\n";
          outs() << MI << "\n";
          outs() << "Operands:\n";
          for (const MachineOperand Op : MI.operands())
          {
            //getConstantVRegVal()
            //getConstantVRegVal(Op.getImm());

          //MachineRegisterInfo(*MF);
            outs() << Op << "\n";
            outs() << "Value is: " << *(size_t*)(Op.getImm()) << "\n";
            outs() << "CI Value is: " << *(size_t*)(Op.getCImm()) << "\n";
            outs() << "Global Value is: " << *(size_t*)(Op.getGlobal()) << "\n";
            outs() << "Value is: " << (Op.getImm()) << "\n";
            outs() << "CI Value is: " << (Op.getCImm()) << "\n";
            outs() << "Global Value is: " << (Op.getGlobal()) << "\n";

          }
      }



      if (MI.mayStore())
        outs() << "Found Store\n";

      if (MI.mayLoad()) {
        outs() << "V Found Load V\n";
        outs() << MI << "\n";
      }

      // MF.getTargetTriple();
      // const auto &Triple = TM.getTargetTriple();
      // if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
      // if ((Triple.getArch() != Triple::x86_64 && Triple.getArch() !=
      // Triple::riscv64) || !Triple.isOSLinux()) return true; // don't do
      // anything to this instruction.
      //SmallVector<MachineOperand, 8> Ops;

      // getRegForValue();
      //Ops.push_back(MachineOperand::CreateReg(llvm::Register(0), true));
      //Ops.push_back(MachineOperand::CreateReg(llvm::Register(0), true));
      // Ops.push_back(MachineOperand::CreateReg(getRegForValue(0,false));
      // Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
      //										/*isDef=*/false));
      //MachineInstrBuilder MIB =
      //    BuildMI(MBB, MI, DL, XII->get(TargetOpcode::PATCHABLE_EVENT_CALL));
      //for (auto &MO : Ops)
      //  MIB.add(MO);

      // BuildMI(MBB, MI, XII->get(X86::fun))

      // if (MI.isReturn() && (MF.getName() == "count")) {
      //   outs() << "Found Return\n";
      //   // addi a0, a0, 2
      //   BuildMI(MBB, MI, DL, XII->get(RISCV::ADDI), RISCV::X10)
      //       .addReg(RISCV::X10)
      //       .addImm(2);
      // }
    }

  }
    return false;
}
} // namespace

INITIALIZE_PASS(X86CustomPass, "X86-custompass",
                X86_MACHINEINSTR_CUSTOM_PASS_NAME, true, true)

namespace llvm {
FunctionPass *createX86CustomPass() { return new X86CustomPass(); }
} // namespace llvm