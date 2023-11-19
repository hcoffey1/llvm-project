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
#define X86_MACHINEINSTR_CUSTOM_PASS_STATICMIXCHECK_NAME "Custom X86 pass - static mix check"

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
  uint64_t TotalInstCount = 0;
  uint64_t CounterInstCount = 0;
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
bool isCounter(MachineInstr &MI) {
  std::string tmp_str;
  raw_string_ostream ss(tmp_str);
  ss << MI << "\n";

  // tmp_str = reduce(tmp_str, " ", " \t");
  // std::string split = " ";
  // outs() << tmp_str << "\n";
  // outs() << tmp_str.find(split) << "\n";
  // size_t start = (tmp_str.find(split)) + 1;
  // size_t end = tmp_str.find(split, start);
  // std::string token = tmp_str.substr(0, tmp_str.find(split));

  // Use hardcodings based on current MIR InlineASM format
  // INLINEASM &"#ZRAY_COUNTER_START" ...
  // outs() << tmp_str << "\n";
  // outs() << start << split << end << split << token << "\n";

  if (tmp_str.size() != 60) {
      return false;
  }
  std::string token = tmp_str.substr(13 /* Start idx - first 13 chars are 'INLINEASM &"#' */, 18 /* length */);
  // if (tmp_str.find("ZRAY_COUNTER_END") != std::string::npos) {
  if (token.find("ZRAY_COUNTER_START") != std::string::npos) {
    // outs() << "Found a counter" << "\n";
    return true;
  }

  return false;
}

bool bbHasCounter(MachineInstr &MI) {
  std::string tmp_str;
  raw_string_ostream ss(tmp_str);
  ss << MI << "\n";

  // tmp_str = reduce(tmp_str, " ", " \t");
  // std::string split = " ";
  // size_t start = (tmp_str.find(split)) + 1;
  // size_t end = tmp_str.find(split, start);

  // Use hardcodings based on current MIR InlineASM format
  // INLINEASM &"#BB_HAS_ZRAY_COUNTER" ...
  if (tmp_str.size() != 61) {
      return false;
  }
  std::string token = tmp_str.substr(13 /* Starting index */, 19 /* length */);
  if (token.find("BB_HAS_ZRAY_COUNTER") != std::string::npos) {
    // outs() << "Found end marker" << "\n";
    return true;
  }

  return false;
}

bool isToolPassBegin(MachineInstr &MI) {
  std::string tmp_str;
  raw_string_ostream ss(tmp_str);
  ss << MI << "\n";

  std::string token = tmp_str.substr(13 /* Starting index */, 15 /* length */);
  if (token.find("TOOL_PASS_BEGIN") != std::string::npos) {
    // outs() << "Found begin marker" << "\n";
    return true;
  }

  return false;
}

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
    inProfile.TotalInstCount = 0;
    inProfile.CounterInstCount = 0;

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

// Global variable to store last BBTag, in case block is split
BBTag lastbbtag;
// Global variable to detect whether BBTag was recently recorded
long recentBBFlag;

void parseMBB(MachineBasicBlock &MBB, std::vector<BBTag> &idVec) {
  BBTag bbtag;
  bool has_counter = false;
  for (auto &MI : MBB) {
    if (MI.isInlineAsm()) {
      bbtag = getBBTag(MI);
      if (bbtag.ceID != -1) {
#if 0
        outs() << "CE ID is " << bbtag.ceID << "\n";
        outs() << MI << "\n";
#endif
        idVec.push_back(bbtag);
        lastbbtag = bbtag;
        recentBBFlag = 3;
        break;
      }
    }
  }
  if((recentBBFlag > 0) && idVec.empty()) {
    int inst_count = 0;
    // This will help us filter out blocks which may have resulted from a split
    --recentBBFlag;
    // Look for ending marker
    for (MachineBasicBlock::reverse_iterator I = MBB.rbegin(); I != MBB.rend(); I++) {
        if ((*I).isInlineAsm()) {
            has_counter |= bbHasCounter(*I);
        }
        // Ugly hack to prevent spending too much time looking for ending
        // marker, since it should be very close to the end, x86_64 has 68 registers
        ++inst_count;
        if(inst_count >= 20) {
            break;
        }
    }
    if(has_counter) idVec.push_back(lastbbtag);
  }
}

char X86CustomPass::ID = 0;
// uint64_t totalLoadCount = 0;
// uint64_t totalStoreCount = 0;

// TODO: Optimize how we update the log file so we are not reading/writing the
// whole thing each time
bool X86CustomPass::runOnMachineFunction(MachineFunction &MF) {

  //Guard to catch empty functions passed in
  if (MF.empty()) {
    return false;
  }
  // uint64_t functionLoadCount = 0;
  // uint64_t functionStoreCount = 0;

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

  // outs() << "MF: " << MF.getName() << "\n";
  // bool thread_exit_frame_flag = false;
  bool toolPassBeginEncountered = false;

  // Iterate over MBB
  for (auto &MBB : MF) {
    // outs() << "V Parsing block V\n";
    // outs() << MBB << "\n";

    // If CE tags are present, update profile for CEs
    std::vector<BBTag> bbTagVec;
    parseMBB(MBB, bbTagVec);

    // outs() << "MBB: " << MBB.getName() << "\n";
    // for (auto &MI : MBB) {
    //     outs() << "MI: " << MI << "\n";
    // }

    // Track number of frame-setup pushes before thread_exit
    int num_frame_setup_push = 0;

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

      size_t loads = 0;
      size_t bytes_read = 0;
      size_t stores = 0;
      size_t bytes_written = 0;
      size_t counters = 0;
      size_t total_insns = 0;
      size_t counter_insns = 0;
      bool init = false;

      size_t stack_stores = 0;
      size_t stack_bytes_written = 0;
      if (pdVec[bbTagVec[0].ceID].IsIndirect) {
        toolPassBeginEncountered = true;
      }

      // outs() << "MBB: " << MBB.getName() << "\n";
      // MBB.print(outs());
      for (auto &MI : MBB) {

        if (MI == MBB.begin() && init) {
          outs() << "Loop!\n";
          break;
        }

        init = true;

        if(MI.isDebugInstr()) continue;
        // outs() << "MI: " << MI << "\n";
        ++total_insns;

        if(MI.getOpcode() == TargetOpcode::G_INTRINSIC)
        {
            outs() << "G_INTRINSIC---------------------------\n";
            outs() << MI << "\n";
          if(MI.mayLoadOrStore())
          {
            for (auto mop : MI.memoperands()) {
              if(mop->isStore()){
                outs() << "INTRINSIC_OP_STORE\n";
              }
              if(mop->isLoad()){
                outs() << "INTRINSIC_OP_LOAD\n";
              }
            }
          }
        }
        if(!MI.mayLoadOrStore() && !MI.isCall() && !MI.isReturn() && !MI.isInlineAsm()) {
          continue;
        }

        if (MI.isInlineAsm())
        {
          // outs() << MI << "\n";
          if (isCounter(MI))
          {
            counters++;
          }
          if (isToolPassBegin(MI))
          {
              toolPassBeginEncountered = true;
              bytes_read = 0;
              loads = 0;
              bytes_written = 0;
              stores = 0;
              counters = 0;
          }
          continue;
        }

        //outs() << "MIR: bbTagVec Size: " << bbTagVec.size() << "\n";
        if (MI.mayLoadOrStore()) {
          for (auto mop : MI.memoperands()) {
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            ss << MI << "\n";
            std::string mi_str = reduce(tmp_str, " ", " \t");
            // Check for CounterArray or CounterArrayRegionOffset in MI
            // If present, do not increment
            // We still miss one extra load and store, so subtract those at the end
            if ((mi_str.find("RuntimeArray") != std::string::npos) || (mi_str.find("CounterArray") != std::string::npos) || (mi_str.find("TimingProfile") != std::string::npos) || (mi_str.find("tool_dyn.cc") != std::string::npos)) {
                ++counter_insns;
                continue;
            }
            if(mop->isStore()){
              // Do not record stack accesses
              if ((mi_str.find("into %stack") != std::string::npos) || (mi_str.find("into stack") != std::string::npos) || (mi_str.find("into %fixed-stack") != std::string::npos)) {
                continue;
              }
              tmp_str.clear();
              ss << MI.getOperand(0) << "\n";
              // if (tmp_str.find("$rsp") != std::string::npos) {
              if (tmp_str.substr(0, 4) == "$rsp") {
                continue;
              }
              // outs() << "Store MI: " << MI << "\n";
              // outs() << "Num Operands: " << MI.getNumOperands() << "\n";
              // outs() << "Operand 0: " << MI.getOperand(0) << "\n";
              // outs() << "Store MI: " << MI << " size: " << mop->getSize() << "\n";
              bytes_written += mop->getSize();
              stores++;
            }
            if(mop->isLoad()){
              // Do not record stack accesses
              if ((mi_str.find("from %stack") != std::string::npos) || (mi_str.find("from stack") != std::string::npos) || (mi_str.find("from %fixed-stack") != std::string::npos)) {
                continue;
              }
              // outs() << "Load MI: " << MI << "\n";
              // outs() << "Load MI: " << MI << " size: " << mop->getSize() << "\n";
              bytes_read += mop->getSize();
              loads++;
            }
          }
        } 
        /* Ignore call and return instructions
        // X86 pushes/pops the return address to/from stack on call/return
        if((MI.isCall() && (MI.getOpcode() != TargetOpcode::PATCHABLE_EVENT_CALL)) || (MI.getOpcode() == X86::PUSH64r)) {
          std::string tmp_str;
          raw_string_ostream ss(tmp_str);
          ss << MI << "\n";
          tmp_str = reduce(tmp_str, " ", " \t");
          if (tmp_str.find("on_thread_exit") != std::string::npos) {
            bytes_written -= 8 * num_frame_setup_push;
            stores -= num_frame_setup_push;
            thread_exit_frame_flag = true;
            // bytes_read -= 8 * num_frame_setup_push;
            // loads -= num_frame_setup_push;
            continue;
          }
          if ((tmp_str.find("timingEvent") != std::string::npos) || (tmp_str.find("tool_dyn.cc") != std::string::npos)) {
            continue;
          }
          if ((MI.getOpcode() == X86::PUSH64r) && (tmp_str.substr(0, 11).find("frame-setup") != std::string::npos)) {
              ++num_frame_setup_push;
            // continue;
          }
          // outs() << "Call/Push MI: " << MI << "\n";
          bytes_written += 8;
          stores++;
        }
        if((MI.isReturn() && (MI.getOpcode() != TargetOpcode::PATCHABLE_RET)) || (MI.getOpcode() == X86::POP64r)) {
          if (MI.getOpcode() == X86::POP64r){
            std::string tmp_str;
            raw_string_ostream ss(tmp_str);
            ss << MI << "\n";
            tmp_str = reduce(tmp_str, " ", " \t");
            if (thread_exit_frame_flag && (tmp_str.substr(7, 13).find("frame-destroy") != std::string::npos)) {
              continue;
            }
          }
          // outs() << "Ret/Pop MI: " << MI << "\n";
          bytes_read += 8;
          loads++;
        }
        */
        // if(bbTagVec.back().ceID == 4) {
        //   outs() << MI << "\n";
        //   outs() << "L: " << loads << " S: " << stores << "\n";
        // }
      }

      for (auto ID : bbTagVec) {
        // outs() << "MIR: Store ID.sf: " << ID.ceID << " : " << ID.sf << "\n";
        // outs() << "PostDomSetID is " << pdVec[ID.ceID].PostDomSetID << " and PragmaRegionID is " << pdVec[ID.ceID].PragmaRegionID << "\n";
        // outs() << "Observed loads: " << loads - counters << ", bytes read: " << bytes_read - 8*counters << ", stores: " << stores - counters << ", bytes written: " << bytes_written - 8*counters << ", counters: " << counters << "\n";
        // outs() << "Recorded loads " << (loads - 3 * counters)*ID.sf << "\n";
        // outs() << "Recorded stores " << (stores - counters)*ID.sf << "\n";
        // outs() << "Counters: " << counters << "\n";
        if (counters <= loads) {
            pdVec[ID.ceID].LoadCount += (loads - counters)*ID.sf;
            bytes_read -= 8 * counters;
            pdVec[ID.ceID].MemInstructionCount += (loads - counters)*ID.sf;
        }
        if (counters <= stores) {
            pdVec[ID.ceID].StoreCount += (stores - counters)*ID.sf;
            bytes_written -= 8 * counters;
            pdVec[ID.ceID].MemInstructionCount += (stores - counters)*ID.sf;
        }
	    // functionStoreCount += (stores - counters)*ID.sf;
	    // functionLoadCount += (loads - 3 * counters)*ID.sf;
	    // totalStoreCount += (stores - counters)*ID.sf;
	    // totalLoadCount += (loads - 3 * counters)*ID.sf;
        // outs() << "MIR: " << "ID " << ID.ceID << " StoreCount after: " << pdVec[ID.ceID].StoreCount << "\n";
        // outs() << "MIR: " << "ID " << ID.ceID << " LoadCount after: " << pdVec[ID.ceID].LoadCount << "\n";
        pdVec[ID.ceID].CounterInstCount += counter_insns + counters; // Account for Inc machine instruction
        pdVec[ID.ceID].TotalInstCount += total_insns - (4 * counters); // Do not include inline ASM statements like ZRAY_COUNTER_*, BB_TAG *, BB_HAS_ZRAY_COUNTER
        pdVec[ID.ceID].BytesWritten += (bytes_written)*(ID.sf);
        pdVec[ID.ceID].BytesRead += (bytes_read)*(ID.sf);
      }
    }
  }
  // outs() << "Function: " << MF.getName().str() << "\n";
  // outs() << "Loads      : " << functionLoadCount << "\n";
  // outs() << "Stores     : " << functionStoreCount << "\n";
  // outs() << "Total:\n";
  // outs() << "Loads      : " << totalLoadCount << "\n";
  // outs() << "Stores     : " << totalStoreCount << "\n";

  writeLogFile(logFileName);
  return false;
}

class X86CustomPassStaticMixCheck : public MachineFunctionPass {
public:
  static char ID;

  X86CustomPassStaticMixCheck() : MachineFunctionPass(ID) {
    initializeX86CustomPassStaticMixCheckPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  StringRef getPassName() const override {
    return X86_MACHINEINSTR_CUSTOM_PASS_NAME;
  }
};

char X86CustomPassStaticMixCheck::ID = 0;

void X86CustomPassStaticMixCheck::getAnalysisUsage(AnalysisUsage &AU) const
{
    MachineFunctionPass::getAnalysisUsage(AU);
}

ProfileData StaticMixAppInfo;
size_t BasicBlockCountApp = 0;

void writeStaticMixInfo(MachineFunction &MF, const ProfileData &Prof, size_t BBCount)
{
    std::ofstream LogFile;
    LogFile.open("./StaticMixInfo.log", std::ios::out | std::ios::app);

    LogFile << "Function: " << MF.getName().str() << "\n";
    LogFile << "Loads      : " << Prof.LoadCount << "\n";
    LogFile << "Stores     : " << Prof.StoreCount << "\n";
    LogFile << "BasicBlocks: " << BBCount << "\n";
    LogFile << "Total\n";
    LogFile << "Loads      : " << StaticMixAppInfo.LoadCount << "\n";
    LogFile << "Stores     : " << StaticMixAppInfo.StoreCount << "\n";
    LogFile << "BasicBlocks: " << BasicBlockCountApp << "\n";

    LogFile.close();
}

bool X86CustomPassStaticMixCheck::runOnMachineFunction(MachineFunction &MF) {

  ProfileData StaticMixFuncInfo;
  size_t BasicBlockCountFunc = 0;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {

        if (MI.isInlineAsm())
        {
          continue;
        }
        if (MI.mayLoad()) {
          StaticMixAppInfo.LoadCount+=1;
          StaticMixFuncInfo.LoadCount+=1;
        }
        if (MI.mayStore()) {
          StaticMixAppInfo.StoreCount+=1;
          StaticMixFuncInfo.StoreCount+=1;
        }
    }
    BasicBlockCountFunc++;
    BasicBlockCountApp++;
  }

  writeStaticMixInfo(MF, StaticMixFuncInfo, BasicBlockCountFunc);

  return false;
}

} // namespace

INITIALIZE_PASS(X86CustomPass, "X86-custompass",
                X86_MACHINEINSTR_CUSTOM_PASS_NAME, true, true)

INITIALIZE_PASS(X86CustomPassStaticMixCheck, "X86-custompass-staticmixcheck",
                X86_MACHINEINSTR_CUSTOM_PASS_STATICMIXCHECK_NAME, true, true)

namespace llvm {
FunctionPass *createX86CustomPass() { return new X86CustomPass(); }
FunctionPass *createX86CustomPassStaticMixCheck() { return new X86CustomPassStaticMixCheck(); }
} // namespace llvm
