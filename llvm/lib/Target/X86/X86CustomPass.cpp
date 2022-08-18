#include "X86.h"
#include "X86InstrInfo.h"

#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#include <fstream>
using namespace llvm;

#define X86_MACHINEINSTR_CUSTOM_PASS_NAME "Custom X86 pass"

namespace {

    struct Profile_Data {
        uint64_t pdSet;
        uint64_t pragmaRegion;
        uint64_t groupID;
        uint64_t storeCount;
        uint64_t loadCount;
        uint64_t intInst;
        uint64_t fpInst;
        uint64_t termInst;
        uint64_t memInst;
        uint64_t castInst;
        uint64_t globalOpRead;
        uint64_t globalOpWrite;
        uint64_t stackRead;
        uint64_t stackWrite;
        uint64_t heapRead;
        uint64_t heapWrite;
        uint64_t otherInst;
    };

    std::vector<Profile_Data> pdVec;

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

    // Remove leading/trailing whitespace from string
    //  https://stackoverflow.com/questions/1798112/removing-leading-and-trailing-spaces-from-a-string
    std::string trim(const std::string &str, const std::string &whitespace) {
        const auto strBegin = str.find_first_not_of(whitespace);
        if (strBegin == std::string::npos)
            return ""; // no content

        const auto strEnd = str.find_last_not_of(whitespace);
        const auto strRange = strEnd - strBegin + 1;

        return str.substr(strBegin, strRange);
    }

    std::string reduce(const std::string &str, const std::string &fill,
            const std::string &whitespace) {
        // trim first
        auto result = trim(str, whitespace);

        // replace sub ranges
        auto beginSpace = result.find_first_of(whitespace);
        while (beginSpace != std::string::npos) {
            const auto endSpace = result.find_first_not_of(whitespace, beginSpace);
            const auto range = endSpace - beginSpace;

            result.replace(beginSpace, range, fill);

            const auto newStart = beginSpace + fill.length();
            beginSpace = result.find_first_of(whitespace, newStart);
        }

        return result;
    }

    int getBBTag(MachineInstr &MI) {
        std::string tmp_str;
        raw_string_ostream ss(tmp_str);
        ss << MI << "\n";

        tmp_str = reduce(tmp_str, " ", " \t");
        std::string split = " ";
        std::string token = tmp_str.substr(0, tmp_str.find(split));

        size_t state = 0;
        size_t pos = 0;

        bool foundTag = false;
        while ((pos = tmp_str.find(split)) != std::string::npos) {
            token = tmp_str.substr(0, pos);

            if (foundTag) {
                token.pop_back();
                return atoi(token.c_str());
            }

            if (token.find("BB_TAG") != std::string::npos) {
                foundTag = true;
            }

            tmp_str.erase(0, pos + split.length());
        }

        return -1;
    }

    void readLogFile(std::string file) {
        std::ifstream regionLogFile;
        regionLogFile.open(file, std::ios::binary);
        Profile_Data inProfile;
        while (regionLogFile.read((char *)&inProfile, sizeof(Profile_Data))) {
            // Clear instruction types we will replace with MIR counts
            inProfile.memInst = 0;
            inProfile.loadCount = 0;
            inProfile.storeCount = 0;

            pdVec.push_back(inProfile);
        }
        regionLogFile.close();
    }

    void writeLogFile(std::string file) {
        std::ofstream regionLogFile;
        regionLogFile.open(file, std::ios::out | std::ios::binary);
        for (auto pd : pdVec) {
            regionLogFile.write((char *)(&pd), sizeof(Profile_Data));
        }
        regionLogFile.close();
    }

    void parseMBB(MachineBasicBlock &MBB, std::vector<int> &idVec) {
        int ceID = -1;
        for (auto &MI : MBB) {
            if (MI.isInlineAsm()) {
                outs() << "V Found inline assembly V\n";
                outs() << MI << "\n";

                ceID = getBBTag(MI);
                outs() << "CE ID is " << ceID << "\n";

                if (ceID != -1) {
                    idVec.push_back(ceID);
                    //MI.eraseFromParent();
                    //break;
                }
            }
        }
        // return ceID;
    }

#if 0
    void cleanMBB(MachineBasicBlock &MBB)
    {
        int ceID = -1;
        for (auto &MI : MBB) {
            while(
                    if (MI.isInlineAsm()) {
                    outs() << "V Found inline assembly V\n";
                    outs() << MI << "\n";

                    ceID = getBBTag(MI);
                    outs() << "CE ID is " << ceID << "\n";

                    if (ceID != -1) {
                    idVec.push_back(ceID);
                    }
                    }
                    }
                    }
#endif

                    int updateCounts(MachineBasicBlock &MBB, const int ceID) {

                    bool init = false;
                    // Using for(auto) leads to infinite loop repeating same basic block
                    // for(MachineBasicBlock::iterator b = MBB.begin(), e = MBB.end(); b != e;
                    // ++b)
                    //{
                    //   if (b->mayStore()) {
                    //     pdVec[ceID].storeCount++;
                    //     pdVec[ceID].memInst++;
                    //   }
                    //   if (b->mayLoad()) {
                    //     pdVec[ceID].loadCount++;
                    //     pdVec[ceID].memInst++;
                    //   }

                    //  if(b == MBB.begin() && init)
                    //  {
                    //    outs() << "Loop!\n";
                    //    break;
                    //  }

                    //  init = true;
                    //}
                    for (auto &MI : MBB) {
                        outs() << MI << "\n";
                        if (MI.mayStore()) {
                            pdVec[ceID].storeCount++;
                            pdVec[ceID].memInst++;
                        }
                        if (MI.mayLoad()) {
                            pdVec[ceID].loadCount++;
                            pdVec[ceID].memInst++;
                        }

                        if (MI == MBB.begin() && init) {
                            outs() << "Loop!\n";
                            break;
                        }

                        init = true;
                    }
                    }

                    char X86CustomPass::ID = 0;

                    // bool init = false;

                    // TODO: Optimize how we update the log file so we are not reading/writing the
                    // whole thing each time
                    bool X86CustomPass::runOnMachineFunction(MachineFunction &MF) {

                        std::string logFileName = "tool_file";
                        outs() << "In " << MF.getFunction().getName() << " Function\n";

                        if (pdVec.empty()) {
                            outs() << "Reading logfile\n";
                            readLogFile(logFileName);
                        }

                        // MachineRegisterInfo(*MF);

                        int ceID;
                        for (auto &MBB : MF) {
                            // outs() << MBB << "\n";
                            // const TargetInstrInfo *XII = MF.getSubtarget().getInstrInfo();
                            // DebugLoc DL;
                            std::vector<int> idVec;
                            outs() << "Parsing block\n";
                            parseMBB(MBB, idVec);

                            if (!idVec.empty()) {
                                outs() << "Updating counts\n";

                                bool init = false;
                                for (auto &MI : MBB) {
                                    outs() << MI << "\n";
                                    if (MI.mayStore()) {
                                        for(auto ID : idVec)
                                        {
                                            pdVec[ID].storeCount++;
                                            pdVec[ID].memInst++;
                                        }
                                    }
                                    if (MI.mayLoad()) {
                                        for(auto ID : idVec)
                                        {
                                            pdVec[ID].loadCount++;
                                            pdVec[ID].memInst++;
                                        }
                                    }

                                    if (MI == MBB.begin() && init) {
                                        outs() << "Loop!\n";
                                        break;
                                    }

                                    init = true;
                                }

                                // updateCounts(MBB, ceID);
                                outs() << "Done\n";
                            }
                        }

                        outs() << "Writing log file\n";
                        writeLogFile(logFileName);
                        // pdVec.clear();
                        return false;
                    }
} // namespace

INITIALIZE_PASS(X86CustomPass, "X86-custompass",
        X86_MACHINEINSTR_CUSTOM_PASS_NAME, true, true)

namespace llvm {
    FunctionPass *createX86CustomPass() { return new X86CustomPass(); }
} // namespace llvm

#if 0
for (auto &MI : MBB) {
    if (MI.isInlineAsm()) {
        outs() << "V Found inline assembly V\n";
        outs() << MI << "\n";

        ceID = getBBTag(MI);
        outs() << "CE ID is " << ceID << "\n";

        if (ceID != -1) {
            MI.eraseFromParent();
            break;
        }
    }

#if 0
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
#endif

    /*
       if (MI.mayStore())
       outs() << "Found Store\n";

       if (MI.mayLoad()) {
       outs() << "V Found Load V\n";
       outs() << MI << "\n";
       }
       */

    // MF.getTargetTriple();
    // const auto &Triple = TM.getTargetTriple();
    // if (Triple.getArch() != Triple::x86_64 || !Triple.isOSLinux())
    // if ((Triple.getArch() != Triple::x86_64 && Triple.getArch() !=
    // Triple::riscv64) || !Triple.isOSLinux()) return true; // don't do
    // anything to this instruction.
    // SmallVector<MachineOperand, 8> Ops;

    // getRegForValue();
    // Ops.push_back(MachineOperand::CreateReg(llvm::Register(0), true));
    // Ops.push_back(MachineOperand::CreateReg(llvm::Register(0), true));
    // Ops.push_back(MachineOperand::CreateReg(getRegForValue(0,false));
    // Ops.push_back(MachineOperand::CreateReg(getRegForValue(I->getArgOperand(1)),
    //										/*isDef=*/false));
    // MachineInstrBuilder MIB =
    //    BuildMI(MBB, MI, DL, XII->get(TargetOpcode::PATCHABLE_EVENT_CALL));
    // for (auto &MO : Ops)
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
#endif
