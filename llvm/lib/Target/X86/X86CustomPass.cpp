// Hayden Coffey
#include "X86.h"
#include "X86InstrInfo.h"

// #include <llvm/Analysis/ScalarEvolutionExpressions.h>
//#include <llvm/Analysis/ScalarEvolution.h>
// #include <llvm/Analysis/PostDominators.h>

// #include "llvm/CodeGen/MachineDominators.h"
// #include "llvm/CodeGen/MachineLoopInfo.h"

#include "llvm/CodeGen/FastISel.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

#include <fstream>
using namespace llvm;

#define X86_MACHINEINSTR_CUSTOM_PASS_NAME "Custom X86 pass"

namespace {

constexpr int checksum(const char *data, size_t length) {
  int sum = 0;
  for (size_t i = 0; i < length; i++) {
    sum ^= data[i];
  }
  return sum;
}

struct ProfileData {
  uint64_t PostDomSetID = 0;
  uint64_t PragmaRegionID = 0;
  uint64_t GroupNumber = 0;
  uint64_t StoreCount = 0;
  uint64_t LoadCount = 0;
  uint64_t BytesRead = 0;
  uint64_t BytesWritten = 0;
  uint64_t IntInstructionCount = 0;
  uint64_t FpInstructionCount = 0;
  uint64_t TermInstructionCount = 0;
  uint64_t MemInstructionCount = 0;
  uint64_t CastInstructionCount = 0;
  uint64_t GlobalOpReadCount = 0;
  uint64_t GlobalOpWriteCount = 0;
  uint64_t StackReadCount = 0;
  uint64_t StackWriteCount = 0;
  uint64_t HeapReadCount = 0;
  uint64_t HeapWriteCount = 0;
  uint64_t OtherInstCount = 0;
  uint64_t IntrinsicLoad = 0;
  uint64_t IntrinsicStore = 0;
  bool IsIndirect;
  bool EnableMIRPass;
};

std::vector<ProfileData> pdVec;
std::vector<std::string> funcNameVec;

bool FirstRun = true;

class X86CustomPass : public MachineFunctionPass {
public:
  static char ID;

  X86CustomPass() : MachineFunctionPass(ID) {
    initializeX86CustomPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  StringRef getPassName() const override {
    return X86_MACHINEINSTR_CUSTOM_PASS_NAME;
  }
};

void X86CustomPass::getAnalysisUsage(AnalysisUsage &AU) const
{
    MachineFunctionPass::getAnalysisUsage(AU);
    //AU.addRequired<MachineLoopInfo>();
    // Specify we need the loopinfo pass to run before this pass
    //AU.addRequired<MachineDominatorTree>();
    //AU.addPreserved<MachineDominatorTree>();

    //AU.addRequired<MachineLoopInfo>();
    //AU.addPreserved<MachineLoopInfo>();

    //AU.addRequired<LoopInfoWrapperPass>();
    //AU.addRequired<ScalarEvolutionWrapperPass>();
    //AU.addRequired<PostDominatorTreeWrapperPass>();
}

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

struct BBTag {
  int ceID;
  size_t sf;
};

BBTag getBBTag(MachineInstr &MI) {
  std::string tmp_str;
  raw_string_ostream ss(tmp_str);
  ss << MI << "\n";

  tmp_str = reduce(tmp_str, " ", " \t");
  std::string split = " ";
  std::string token = tmp_str.substr(0, tmp_str.find(split));

  BBTag bbtag;
  bbtag.ceID = -1;
  bbtag.sf = 0;

  size_t pos = 0;
  size_t state = 0;

  while ((pos = tmp_str.find(split)) != std::string::npos) {
    token = tmp_str.substr(0, pos);
    switch (state) {
    case 0:
      if (token.find("BB_TAG") != std::string::npos) {
        state++;
      }
      break;
    case 1:
      bbtag.ceID = atoi(token.c_str());
      state++;
      break;
    case 2:
      bbtag.sf = atoi(token.c_str());
      return bbtag;
      break;
    }

    tmp_str.erase(0, pos + split.length());
  }

  return bbtag;
}

bool readLogFile(std::string file) {
  std::ifstream regionLogFile;
  regionLogFile.open(file, std::ios::binary);
  ProfileData inProfile;

  size_t FunctionNameLen;
  int InCheck;
  int Check;
  while (regionLogFile.read((char *)&inProfile, sizeof(ProfileData))) {
    // Read in and verify checksum
    regionLogFile.read(reinterpret_cast<char *>(&InCheck), sizeof(InCheck));
    Check = checksum((char *)&inProfile, sizeof(ProfileData));

    if (Check != InCheck) {
      errs() << "X86 MIR Pass: Checksum mismatch!\n";
      errs() << InCheck << " " << Check << "\n";
    }

    regionLogFile.read(reinterpret_cast<char *>(&FunctionNameLen),
                       sizeof(FunctionNameLen));

    std::string FunctionName(FunctionNameLen, '\0');
    regionLogFile.read(&FunctionName[0], FunctionNameLen);

    if(!inProfile.EnableMIRPass)
    {
      regionLogFile.close();
      return false;
    }

    // Clear instruction types we will replace with MIR counts
    inProfile.MemInstructionCount = 0;
    inProfile.LoadCount = 0;
    inProfile.StoreCount = 0;
    inProfile.BytesRead = 0;
    inProfile.BytesWritten = 0;

    pdVec.push_back(inProfile);
    funcNameVec.push_back(FunctionName);
  }
  regionLogFile.close();

  return true;
}

void writeLogFile(std::string file) {
  std::ofstream regionLogFile;
  regionLogFile.open(file, std::ios::out | std::ios::binary);
  for (int i = 0; i < pdVec.size(); i++) {
    regionLogFile.write((char *)(&pdVec[i]), sizeof(ProfileData));

    // Write checksum
    int Check = checksum(reinterpret_cast<const char *>(&pdVec[i]),
                         sizeof(ProfileData));
    regionLogFile.write(reinterpret_cast<const char *>(&Check), sizeof(Check));

    // Write function name to log
    std::string FunctionName = funcNameVec[i];
    size_t FunctionNameLen = FunctionName.size();

    regionLogFile.write(reinterpret_cast<const char *>(&FunctionNameLen),
                        sizeof(FunctionNameLen));
    regionLogFile.write(FunctionName.c_str(), FunctionNameLen);
  }
  regionLogFile.close();
}
/*
    bool isToolFlag(std::string inst, std::string pragmaName)
    {
        using namespace std;
        bool flag = false;
        inst = reduce(inst);
        string split = " ";
        string token = inst.substr(0, inst.find(split));

        size_t state = 0;

        size_t pos = 0;
        while ((pos = inst.find(split)) != std::string::npos)
        {
            token = inst.substr(0, pos);
            switch (state)
            {
            case 0:
                if (token == "asm")
                    state++;
                break;
            //case 1:
            //    if (token == "sideeffect")
            //        state++;
            //    else
            //        return false;
            //    break;
            case 1:
                if (token.find(pragmaName) != string::npos)
                    return true;
                else
                    return false;
                break;
            }

            inst.erase(0, pos + split.length());
        }
        return false;
    }

    void parseBB(BasicBlock &MBB, std::vector<BBTag> &idVec) {
        BBTag bbtag;
        for (auto &MI : MBB) {
            if (MI.isInlineAsm()) {
                bbtag = getBBTag(MI);
                if (bbtag.ceID != -1) {
                    outs() << "CE ID is " << bbtag.ceID << "\n";
                    outs() << MI << "\n";
                    idVec.push_back(bbtag);
                }
            }
        }
    }
    */

void parseMBB(MachineBasicBlock &MBB, std::vector<BBTag> &idVec) {
  BBTag bbtag;
  for (auto &MI : MBB) {
    if (MI.isInlineAsm()) {
      bbtag = getBBTag(MI);
      if (bbtag.ceID != -1) {
#if 0
        outs() << "CE ID is " << bbtag.ceID << "\n";
        outs() << MI << "\n";
#endif
        idVec.push_back(bbtag);
      }
    }
  }
}

char X86CustomPass::ID = 0;

// TODO: Optimize how we update the log file so we are not reading/writing the
// whole thing each time
bool X86CustomPass::runOnMachineFunction(MachineFunction &MF) {

  //Guard to catch empty functions passed in
  if (MF.empty()) {
    return false;
  }

  std::string logFileName = std::getenv("ZRAY_LOGFILE");
  // outs() << "In " << MF.getFunction().getName() << " Function\n";

  // Read in CE profile count data
  if (pdVec.empty()) {
    // outs() << "Reading logfile\n";
    if(!readLogFile(logFileName))
    {
      return false;
    }

    if(FirstRun)
    {
      outs() << "ZRAY: Running MIR Pass...\n";
      FirstRun = false;
    }
  }

  // Iterate over MBB
  for (auto &MBB : MF) {
    // outs() << "V Parsing block V\n";
    // outs() << MBB << "\n";

    // If CE tags are present, update profile for CEs
    std::vector<BBTag> bbTagVec;
    parseMBB(MBB, bbTagVec);

    if (!bbTagVec.empty()) {

#if 0
                outs() << "Tags found. Updating counts...\n";
                outs() << "Tags: ";
                for(auto t : bbTagVec)
                {
                    outs() << t.ceID << " " << t.sf << "\n";
                }
                outs() << "\n";
#endif

      bool init = false;
      for (auto &MI : MBB) {

//        if(MI.getOpcode() == TargetOpcode::G_INTRINSIC)
//        {
//            outs() << "G_INTRINSIC---------------------------\n";
//            outs() << MI << "\n";
//          if(MI.getIntrinsicID() == Intrinsic::memcpy)
//          {
//            outs() << "Found a memcpy in the IR!---------------------------\n";
//          }
//        }
//        if(MI.getOpcode() == TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS)
//        {
//            outs() << "G_INTRINSIC_W_SIDE_EFFECTS---------------------------\n";
//            outs() << MI << "\n";
//          if(MI.getIntrinsicID() == Intrinsic::memcpy)
//          {
//            outs() << "Found a memcpy in the IR!---------------------------\n";
//          }
//        }
//        if(MI.getOpcode() == TargetOpcode::G_INTRINSIC_LRINT)
//        {
//            outs() << "G_INTRINSIC_LRINT---------------------------\n";
//            outs() << MI << "\n";
//          if(MI.getIntrinsicID() == Intrinsic::memcpy)
//          {
//            outs() << "Found a memcpy in the IR!---------------------------\n";
//          }
//        }
//        if(MI.isCall())
//        {
//          outs() << MI << "\n ^^opcode: " << MI.getOpcode() << "\n";
//          outs() << MI.getIntrinsicID() << "\n";
//        }
//
        if (MI.isInlineAsm())
        {
          continue;
        }

        //outs() << "MIR: bbTagVec Size: " << bbTagVec.size() << "\n";

        if (MI.mayStore()) {
          size_t totalBytes = 0;
          for (auto mop : MI.memoperands()) {
            totalBytes += mop->getSize();
          }
          for (auto ID : bbTagVec) {
            //outs() << "MIR: Store ID.sf: " << ID.ceID << " : " << ID.sf << "\n";
            pdVec[ID.ceID].StoreCount += ID.sf;
            pdVec[ID.ceID].MemInstructionCount += ID.sf;
            pdVec[ID.ceID].BytesWritten += (totalBytes * ID.sf);
            break;
          }
        }
        if (MI.mayLoad()) {
          size_t totalBytes = 0;
          for (auto mop : MI.memoperands()) {
            totalBytes += mop->getSize();
          }
          for (auto ID : bbTagVec) {
            //outs() << "MIR: Load ID.sf: " << ID.ceID << " : " << ID.sf << "\n";
            pdVec[ID.ceID].LoadCount += ID.sf;
            pdVec[ID.ceID].MemInstructionCount += ID.sf;
            pdVec[ID.ceID].BytesRead += (totalBytes * ID.sf);
            break;
          }
        }

        if (MI == MBB.begin() && init) {
          outs() << "Loop!\n";
          break;
        }

        init = true;
      }
    }
  }

  writeLogFile(logFileName);
  return false;
}
} // namespace

INITIALIZE_PASS(X86CustomPass, "X86-custompass",
                X86_MACHINEINSTR_CUSTOM_PASS_NAME, true, true)

namespace llvm {
FunctionPass *createX86CustomPass() { return new X86CustomPass(); }
} // namespace llvm
