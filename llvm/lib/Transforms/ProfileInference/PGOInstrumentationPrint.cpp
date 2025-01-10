#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/ProfileInference/PGOInstrumentationPrint.h"
#include "llvm/IR/Constants.h" // 包含 ConstantInt 的定义
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include <string>
#include <unistd.h> 

#include "llvm/Transforms/ProfileInference/CSVUtil.h"


using namespace llvm;
using namespace std;

cl::opt<std::string> PGOBenchmarkName("pgo-benchmark-name", cl::init(""),
                                    cl::value_desc("benchmark name"),
                                    cl::desc("The name of the benchmark"));

cl::opt<std::string> SampleDataFileName("pgo-sample-print", cl::init(""),
                                    cl::value_desc("sample csv filename"),
                                    cl::desc("Print PGO sample data to a file"));

cl::opt<std::string> InstrDataFileName("pgo-instr-print", cl::init(""),
                                           cl::value_desc("filename"),
                                           cl::desc("Print PGO instrumentation data to a file"));


// 使用反向 CFG 后序遍历获得近似拓扑排序
std::vector<llvm::BasicBlock*> getTopologicalOrder(Function &F) {
    std::vector<llvm::BasicBlock*> topoOrder;

    // 从反向图的出口块开始后序遍历
    for (llvm::BasicBlock *BB : llvm::post_order(&F.getEntryBlock())) {
        topoOrder.push_back(BB);
    }

    // 反转以获得从入口到出口的近似拓扑顺序
    std::reverse(topoOrder.begin(), topoOrder.end());
    assert(topoOrder.size() == F.size());
    return topoOrder;
}


std::string getBlockFreqStr(Function& F, BlockFrequencyInfo& BFI) {
    std::string block_freq_str = "";
    // 遍历每个基本块
    for (BasicBlock& BB : F) {
        uint64_t freqVal = 0;
        // 获取基本块的 profile count
        std::optional< uint64_t > freq = BFI.getBlockProfileCount(&BB);
        if (freq.has_value()) {
            freqVal = freq.value();
        }
        block_freq_str += to_string(BB.getNumber()) + ":" + to_string(freqVal) + ",";
        // freqVal = BFI.getBlockFreq(&BB).getFrequency();
        // 打印基本块的频率
        llvm::outs() << "  BasicBlock: " << BB.getNumber() << ", Freq: " << freqVal << "\n";
    }
    block_freq_str.pop_back();
    return block_freq_str;
}

std::string getAjacentStr(Function& F) {
    std::string ajacent_str = "";
    for (BasicBlock& BB : F) {
        ajacent_str += to_string(BB.getNumber()) + ":";
        std::string neighbor_str = "";
        for (BasicBlock* succ : successors(&BB)) {
            neighbor_str += to_string(succ->getNumber()) + ",";
        }
        if (neighbor_str.length() > 0) {
            neighbor_str.pop_back();
            ajacent_str += neighbor_str;
        }
        ajacent_str += ";";
    }
    if (ajacent_str.length() > 0)
        ajacent_str.pop_back();
    return ajacent_str;
}

std::string getZeroFeature(int size) {
    std::string zeroFeature = "";
    for (int i = 0; i < size; i++) {
        zeroFeature += "0,";
    }
    if (zeroFeature.length() > 0) {
        zeroFeature.pop_back();
    }
    return zeroFeature;
}

std::vector<double> getZeroFloatVec(int size) {
    std::vector<double> zeroVec;
    for (int i = 0; i < size; i++) {
        zeroVec.push_back(0);
    }
    return zeroVec;
}

std::vector<int> getZeroIntVec(int size) {
    std::vector<int> zeroVec;
    for (int i = 0; i < size; i++) {
        zeroVec.push_back(0);
    }
    return zeroVec;
}

std::string vec2str(std::vector<double> vec) {
    std::string str = "";
    for (double d : vec) {
        str += to_string(d) + ",";
    }
    if (str.length() > 0) {
        str.pop_back();
    }
    return str;
}

std::string vec2str(std::vector<int> vec) {
    std::string str = "";
    for (int d : vec) {
        str += to_string(d) + ",";
    }
    if (str.length() > 0) {
        str.pop_back();
    }
    return str;
}

std::string getBlockFeatureStr(Function& F, LoopInfo& LI) {

    std::string feature_str = "";
    std::vector<BasicBlock*> topoOrder = getTopologicalOrder(F);
    
    std::unordered_map<BasicBlock*, std::vector<int>> blockFeatureMap;

    for (BasicBlock* BB : topoOrder) {

        int callNumber = 0, arithmeticNumber = 0, logicalNumber = 0;
        int loadNumber = 0, storeNumber = 0, intrinsicNumber = 0;
        int phiNumber = 0, gepNumber = 0, castNumber = 0;
        int instructionNumber = BB->size(), successorNumber = succ_size(BB), predecessorNumber = pred_size(BB);
        int isEntry = BB == &F.getEntryBlock(), isExit = succ_size(BB) == 0;
        int isLoopHeader = LI.isLoopHeader(BB);
        // LLVM 的基本块后继（BasicBlock*）的顺序，与分支指令的后继块列表一致：
        // •	对于条件分支指令：
        // •	getSuccessor(0) 是 true 分支。
        // •	getSuccessor(1) 是 false 分支。
        // •	对于多路分支：
        // •	getDefaultDest() 返回默认目标。
        // •	getCaseSuccessor() 返回具体值对应的目标。
        for (auto & I : *BB) {
            if (isa<CallInst>(I)) {
                callNumber++;
            } else if (isa<BinaryOperator>(I)) {
                arithmeticNumber++;
            } else if (isa<CmpInst>(I)) {
                logicalNumber++;
            } else if (isa<LoadInst>(I)) {
                loadNumber++;
            } else if (isa<StoreInst>(I)) {
                storeNumber++;
            } else if (isa<IntrinsicInst>(I)) {
                intrinsicNumber++;
            } else if (isa<PHINode>(I)) {
                phiNumber++;
            } else if (isa<GetElementPtrInst>(I)) {
                gepNumber++;
            } else if (isa<CastInst>(I)) {
                castNumber++;
            }
        }

        // blockFeatureMap[&BB] = to_string(callNumber) + ","
        //     + to_string(arithmeticNumber) + "," + to_string(logicalNumber) + ","
        //     + to_string(loadNumber) + "," + to_string(storeNumber) + ","
        //     + to_string(intrinsicNumber) + "," + to_string(phiNumber) + ","
        //     + to_string(gepNumber) + "," + to_string(castNumber) + ","
        //     + to_string(instructionNumber) + "," + to_string(successorNumber) + "," + to_string(predecessorNumber) + ","
        //     + to_string(isEntry) + "," + to_string(isExit) + ","
        //     + to_string(isLoopHeader)
        //     ;
        blockFeatureMap[BB] = {
            callNumber, arithmeticNumber, 
            logicalNumber, loadNumber, storeNumber, intrinsicNumber, phiNumber, 
            gepNumber, castNumber, instructionNumber, successorNumber, predecessorNumber, 
            isEntry, isExit, isLoopHeader
        };

    }

    for (BasicBlock* BB : topoOrder) {
        feature_str += to_string(BB->getNumber()) + ":" + vec2str(blockFeatureMap[BB]) + ",";

        std::vector<int> trueSuccFeature = getZeroIntVec(15);
        std::vector<int> falseSuccFeature = getZeroIntVec(15);

        std::string falseSuccFeatureStr;
        // 假设 BB 是一个 BasicBlock*
        if (auto *BI = dyn_cast<BranchInst>(BB->getTerminator())) {
            
            if (BI->isConditional()) {
                Value *Condition = BI->getCondition();
                BasicBlock *TrueSuccessor = BI->getSuccessor(0);
                BasicBlock *FalseSuccessor = BI->getSuccessor(1);
                trueSuccFeature = blockFeatureMap.at(TrueSuccessor);
                falseSuccFeature = blockFeatureMap.at(FalseSuccessor);
                // errs() << "Condition: " << *Condition << "\n";
                // errs() << "True Successor: " << TrueSuccessor->getName() << "\n";
                // errs() << "False Successor: " << FalseSuccessor->getName() << "\n";
            }
            else {
                // errs() << "Unconditional branch to: " << BI->getSuccessor(0)->getName() << "\n";
                trueSuccFeature = blockFeatureMap.at(BI->getSuccessor(0));
            }

            falseSuccFeatureStr = vec2str(falseSuccFeature);
        }

        // 假设 BB 是一个 BasicBlock*
        else if (auto *SI = dyn_cast<SwitchInst>(BB->getTerminator())) {
            Value *Condition = SI->getCondition();
            BasicBlock *DefaultSuccessor = SI->getDefaultDest();
            trueSuccFeature = blockFeatureMap[DefaultSuccessor];
            // errs() << "Condition: " << *Condition << "\n";
            // errs() << "Default Successor: " << DefaultSuccessor->getName() << "\n";
            
            std::vector<double> sum = getZeroFloatVec(15);
            for (auto &Case : SI->cases()) {
                ConstantInt *CaseValue = Case.getCaseValue();
                BasicBlock *CaseSuccessor = Case.getCaseSuccessor();
                for (int i = 0; i < sum.size(); i++) {
                    sum[i] += blockFeatureMap[CaseSuccessor][i];
                }
                // errs() << "Case Value: " << CaseValue->getValue() << ", Successor: " << CaseSuccessor->getName() << "\n";
            }
            // compute the average feature of all case successors
            for (int i = 0; i < sum.size(); i++) {
                sum[i] /= SI->getNumCases();
            }
            falseSuccFeatureStr = vec2str(sum);
        }
        else {
            falseSuccFeatureStr = vec2str(getZeroIntVec(15));
        }
        assert(falseSuccFeatureStr.length() > 0);
        feature_str += vec2str(trueSuccFeature) + "," + falseSuccFeatureStr + ";";
    }

    if (feature_str.length() > 0) {
        feature_str.pop_back();
    }

    return feature_str;

}


std::vector<std::string> getInstFeatureStr(Function& F) {

    std::string instr_adj_str = "";
    std::unordered_map<Instruction*, int> instrIDMap;

    for (BasicBlock& BB : F) {
        for (Instruction& I : BB) {
            instrIDMap[&I] = instrIDMap.size();
        }
    }

    for (BasicBlock& BB : F) {
        for (Instruction& I : BB) {
            int ID = instrIDMap[&I];

        }
    }

    std::string feature_str = "";
    std::string bb_instrs_str = "";
    for (BasicBlock& BB : F) {

        std::string instrsFeatureStr = "";
        bb_instrs_str += to_string(BB.getNumber()) + ":";
        for (Instruction& I : BB) {
            int ID = instrIDMap[&I], parentID = BB.getNumber(), opCode = I.getOpcode();
            int numOperands = I.getNumOperands(), numUses = I.getNumUses();
            bool isTerminator = I.isTerminator(), isUnaryOp = I.isUnaryOp(), isBinaryOp = I.isBinaryOp();
            bool isIntDivRem = I.isIntDivRem(), isShift = I.isShift(), isCast = I.isCast();            
            bool isLogicalShift = I.isLogicalShift(), isArithmeticShift = I.isArithmeticShift(), isBitwiseLogicOp = I.isBitwiseLogicOp();
            bool isAssociative = I.isAssociative(), isCommutative = I.isCommutative(), isIdempotent = I.isIdempotent(), isNilpotent = I.isNilpotent();
            bool mayWriteToMemory = I.mayWriteToMemory(), mayReadFromMemory = I.mayReadFromMemory(), mayReadOrWriteMemory = I.mayReadOrWriteMemory();
            bool isAtomic = I.isAtomic(), isVolatile = I.isVolatile();

            Type* type = I.getType();
            int TypeID = type->getTypeID(), width = type->getScalarSizeInBits();

            std::string instrFeatureStr = to_string(ID) + ":" + to_string(opCode) + "," + to_string(numOperands) + "," + to_string(numUses) + ","
                + to_string(isTerminator) + "," + to_string(isUnaryOp) + "," + to_string(isBinaryOp) + ","
                + to_string(isIntDivRem) + "," + to_string(isShift) + "," + to_string(isCast) + ","
                + to_string(isLogicalShift) + "," + to_string(isArithmeticShift) + "," + to_string(isBitwiseLogicOp) + ","
                + to_string(isAssociative) + "," + to_string(isCommutative) + "," + to_string(isIdempotent) + "," + to_string(isNilpotent) + ","
                + to_string(mayWriteToMemory) + "," + to_string(mayReadFromMemory) + "," + to_string(mayReadOrWriteMemory) + ","
                + to_string(isAtomic) + "," + to_string(isVolatile) + ","
                + to_string(TypeID) + "," + to_string(width)
                ;

            instrsFeatureStr += instrFeatureStr + ";";

            std::string adj_str = to_string(ID) + ":";
            
            for (Value* V : I.operands()) {
                if (Instruction* opI = dyn_cast<Instruction>(V)) {
                    adj_str += to_string(instrIDMap[opI]) + ",";
                }
            }
            for (User* U : I.users()) {
                if (Instruction* userI = dyn_cast<Instruction>(U)) {
                    adj_str += to_string(instrIDMap[userI]) + ",";
                }
            }
            if (adj_str.back() == ',') {
                adj_str.pop_back();
            }
            instr_adj_str += adj_str + ";";
            bb_instrs_str += to_string(ID) + ",";
        }
        if (instrsFeatureStr.length() > 0) {
            instrsFeatureStr.pop_back();
        }
        if (BB.size() > 0) {
            bb_instrs_str.pop_back();
        }
        feature_str += instrsFeatureStr + "|";
        bb_instrs_str += ";";
    }
    if (feature_str.length() > 0) {
        feature_str.pop_back();
    }
    if (instr_adj_str.length() > 0)
        instr_adj_str.pop_back();
    if (bb_instrs_str.length() > 0) {
        bb_instrs_str.pop_back();
    }
    return {feature_str, instr_adj_str, bb_instrs_str};
}

PreservedAnalyses PGOInstrumentationPrint::run(Module &M, ModuleAnalysisManager &MAM) {
    if (InstrDataFileName == "") return PreservedAnalyses::all();
    outs() << "OutputFilename: " << InstrDataFileName << "\n";
    auto getBFI = [&](Function &F) -> BlockFrequencyInfo & {
        return MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager().getResult<BlockFrequencyAnalysis>(F);
    };

    auto getLI = [&](Function &F) -> LoopInfo & {
        return MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager().getResult<LoopAnalysis>(F);
    };

    using namespace std;
    shared_ptr<Row> headLine = make_shared<Row>();
    (*headLine)
        .appendData("func")
        .appendData("block_freq")
        ;
    
    pid_t processID = getpid(); 
    std::string name = InstrDataFileName + "." + to_string(processID);

    bool isExist = CSV::create(name);
    CSV& instr_csv = CSV::getRef();
    // if (!isExist) {
    //     instr_csv.setHead(headLine);
    // }

    for (auto &F : M) {
        if (F.isDeclaration()) {
            outs() << "Skip Declaration Function: " << F.getName() << "\n";
            continue;
        }
        if (F.size() == 0) {
            outs() << "Skip Empty Function: " << F.getName() << "\n";
            continue;
        }
        if (F.size() == 1) {
            outs() << "Skip Function with only one BB: " << F.getName() << "\n";
            continue;
        }

        BlockFrequencyInfo& BFI = getBFI(F);
        LoopInfo& LI = getLI(F);

        // 获取函数的 profile count
        auto funcCount = F.getEntryCount();
        if (funcCount.has_value()) {
            // 打印函数的 profile count
            llvm::outs() << "Function: " << F.getName() << ", Count: " << funcCount.value().getCount() << "\n";
            if (funcCount.value().getCount() == 0) {
                continue;
            }
        } else {
            llvm::outs() << "Function: " << F.getName() << ", Count: [No Profile Data]\n";
            continue;
        }

        shared_ptr<Row> row = make_shared<Row>();
        row->appendData(F.getName().str());

        std::string instr_feature_str, instr_adj_str;
        std::vector<std::string> vec = getInstFeatureStr(F);

        row->appendData("\"" + getBlockFreqStr(F, BFI) + "\"");
        row->appendData("\"" + getBlockFeatureStr(F, LI) + "\"");
        row->appendData("\"" + getAjacentStr(F) + "\"");
        row->appendData("\"" + vec[0] + "\"");
        row->appendData("\"" + vec[1] + "\"");
        row->appendData("\"" + vec[2] + "\"");
        instr_csv.appendRow(row);
    }
    instr_csv.flush();
    return PreservedAnalyses::all();
}


// // 注册 Pass
// llvm::PassPluginLibraryInfo getPGOInstrumentationPrintPluginInfo() {
//     return {LLVM_PLUGIN_API_VERSION, "PGOInstrumentationPrint", LLVM_VERSION_STRING,
//             [](PassBuilder &PB) {
//                 PB.registerPipelineParsingCallback(
//                     [](StringRef Name, FunctionPassManager &FPM,
//                        ArrayRef<PassBuilder::PipelineElement>) {
//                         if (Name == "pgo-instrumentation-print") {
//                             FPM.addPass(PGOInstrumentationPrint());
//                             return true;
//                         }
//                         return false;
//                     });
//             }};
// }

// extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
//     return getPGOInstrumentationPrintPluginInfo();
// }