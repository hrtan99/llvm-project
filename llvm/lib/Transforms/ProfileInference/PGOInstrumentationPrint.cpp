#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Instrumentation/PGOInstrumentation.h"
#include "llvm/Transforms/ProfileInference/PGOInstrumentationPrint.h"
#include "llvm/IR/Constants.h" // 包含 ConstantInt 的定义
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include <unistd.h> 

#include "llvm/Transforms/ProfileInference/CSVUtil.h"


using namespace llvm;


cl::opt<std::string> PGOBenchmarkName("pgo-benchmark-name", cl::init(""),
                                    cl::value_desc("benchmark name"),
                                    cl::desc("The name of the benchmark"));

cl::opt<std::string> SampleDataFileName("pgo-sample-print", cl::init(""),
                                    cl::value_desc("sample csv filename"),
                                    cl::desc("Print PGO sample data to a file"));

cl::opt<std::string> InstrDataFileName("pgo-instr-print", cl::init(""),
                                           cl::value_desc("filename"),
                                           cl::desc("Print PGO instrumentation data to a file"));

PreservedAnalyses PGOInstrumentationPrint::run(Module &M, ModuleAnalysisManager &MAM) {
    if (InstrDataFileName == "") return PreservedAnalyses::all();
    outs() << "OutputFilename: " << InstrDataFileName << "\n";
    auto getBFI = [&](Function &F) -> BlockFrequencyInfo & {
        return MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager().getResult<BlockFrequencyAnalysis>(F);
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
    if (!isExist) {
        instr_csv.setHead(headLine);
    }

    for (auto &F : M) {
        if (F.isDeclaration()) {
            outs() << "Skip Function: " << F.getName() << "\n";
            continue;
        }

        BlockFrequencyInfo& BFI = getBFI(F);
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
        row->appendData("\"" + block_freq_str + "\"");
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