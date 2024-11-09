#ifndef LLVM_TRANSFORMS_UTILS_SAMPLEPROFILEINFERENCE_PROXY_H
#define LLVM_TRANSFORMS_UTILS_SAMPLEPROFILEINFERENCE_PROXY_H

#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/IntrinsicInst.h>
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/SampleProfileInference.h"
#include "llvm/Transforms/ProfileInference/CSVUtil.h"
#include <unistd.h>
#include <type_traits>



extern llvm::cl::opt<std::string> SampleDataFileName;
extern llvm::cl::opt<std::string> PGOBenchmarkName;

namespace llvm {


// Proxy class
template <typename FT>
class SampleProfileInferenceProxy {
public:
    using NodeRef = typename SampleProfileInference<FT>::NodeRef;
    using BasicBlockT = typename SampleProfileInference<FT>::BasicBlockT;
    using Edge = typename SampleProfileInference<FT>::Edge;
    using BlockWeightMap = typename SampleProfileInference<FT>::BlockWeightMap;
    using EdgeWeightMap = typename SampleProfileInference<FT>::EdgeWeightMap;
    using BlockEdgeMap = typename SampleProfileInference<FT>::BlockEdgeMap;

    // Constructor that takes a reference to FunctionT, BlockEdgeMap, and BlockWeightMap
    SampleProfileInferenceProxy(FT &F, BlockEdgeMap &Successors, BlockWeightMap &SampleBlockWeights)
        : inference(F, Successors, SampleBlockWeights), F(F) {}

    // 使用反向 CFG 后序遍历获得近似拓扑排序
    std::vector<llvm::BasicBlock*> getTopologicalOrder() {
        std::vector<llvm::BasicBlock*> topoOrder;

        // 从反向图的出口块开始后序遍历
        for (llvm::BasicBlock *BB : llvm::post_order(&F.getEntryBlock())) {
            topoOrder.push_back(BB);
        }

        // 反转以获得从入口到出口的近似拓扑顺序
        std::reverse(topoOrder.begin(), topoOrder.end());

        return topoOrder;
    }

    // Proxy method to apply the profile inference algorithm
    void apply(BlockWeightMap &BlockWeights, EdgeWeightMap &EdgeWeights) {

        using namespace std;  
        shared_ptr<Row> headLine = make_shared<Row>();
        (*headLine)
            .appendData("func")
            .appendData("block_freq_before")
            .appendData("block_freq_after")
            ;

        pid_t processID = getpid(); // 获取当前进程 ID
        std::string name = PGOBenchmarkName + "_" + to_string(processID) + ".sample.csv";
        bool isExist = CSV::create(SampleDataFileName);
        CSV& sample_csv = CSV::getRef();
        if (!isExist) {
            sample_csv.setHead(headLine);
        }

        shared_ptr<Row> row = make_shared<Row>();
        row->appendData(F.getName().str());

        outs() << "\nApplying profi inference\n";
        outs() << "\nFunction: " << F.getName() << "\n";
        outs() << "\nBlockWeights before inference:\n";

        std::string block_before_str = "";


        std::string feature_str = "";
        std::string ajacent_str = "";
        // Only execute this part if BasicBlockT is of type BasicBlock
        if constexpr (std::is_same_v<BasicBlockT, llvm::BasicBlock>) {
            std::vector<BasicBlock*> topoOrder = getTopologicalOrder();
            
            for (BasicBlock* BB : topoOrder) {
                block_before_str += to_string(BB->getNumber()) + ":" + to_string(BlockWeights[BB]) + ",";
                outs() << BB->getNumber() << ": " << BlockWeights[BB] << "\n";
                int callNumber = 0, arithmeticNumber = 0, logicalNumber = 0;
                int loadNumber = 0, storeNumber = 0, intrinsicNumber = 0;
                int phiNumber = 0, gepNumber = 0, castNumber = 0;
                int instructionNumber = BB->size(), successorNumber = succ_size(BB), predecessorNumber = pred_size(BB);
                bool isEntry = BB == &F.getEntryBlock(), isExit = succ_size(BB) == 0;
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
                feature_str += to_string(BB->getNumber()) + ":" + to_string(callNumber) + ","
                    + to_string(arithmeticNumber) + "," + to_string(logicalNumber) + ","
                    + to_string(loadNumber) + "," + to_string(storeNumber) + ","
                    + to_string(intrinsicNumber) + "," + to_string(phiNumber) + ","
                    + to_string(gepNumber) + "," + to_string(castNumber) + ","
                    + to_string(instructionNumber) + "," + to_string(successorNumber) + "," + to_string(predecessorNumber) + ","
                    + to_string(isEntry) + "," + to_string(isExit) + ";";
            }

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
        }
        else {
            outs() << "BasicBlockT is not of type BasicBlock\n";
        }
        if (block_before_str.length() > 0)
            block_before_str.pop_back();
        if (feature_str.length() > 0)
            feature_str.pop_back();
        if (ajacent_str.length() > 0)
            ajacent_str.pop_back();

        // for (auto &I : BlockWeights) {
        //     outs() << I.first->getNumber() << ": " << I.second << "\n";
        //     int blockNumber = I.first->getNumber();

        //     block_before_str += to_string(I.first->getNumber()) + ":" + to_string(I.second) + ",";
        // }

        outs() << "\nEdgeWeights before inference:\n";
        for (auto &I : EdgeWeights) {
            outs() << I.first.first->getNumber() << "->" << I.first.second->getNumber()
                << ": " << I.second << "\n";
        }

        int totalExcutionCount = 0;
        for (auto &I : BlockWeights) {
            totalExcutionCount += I.second;
        }

        int blockWeightSizeBefore = BlockWeights.size(), BBSizeBefore = F.size();
        inference.apply(BlockWeights, EdgeWeights);
        int blockWeightSizeAfter = BlockWeights.size(), BBSizeAfter = F.size();

        if (totalExcutionCount && (blockWeightSizeBefore != blockWeightSizeAfter || BBSizeBefore != BBSizeAfter)) {
            outs() << "Error: BlockWeight size or BB size changed after inference for function " << F.getName() << "\n";
            outs() << "BlockWeight size before: " << blockWeightSizeBefore << ", after: " << blockWeightSizeAfter << "\n";
            outs() << "BB size before: " << BBSizeBefore << ", after: " << BBSizeAfter << "\n";
        }

        std::string block_after_str = "";
        
        outs() << "\nBlockWeights after inference:\n";
        if constexpr (std::is_same_v<BasicBlockT, llvm::BasicBlock>) {
            std::vector<BasicBlock*> topoOrder = getTopologicalOrder();
            
            for (BasicBlock* BB : topoOrder) {
                outs() << BB->getNumber() << ": " << BlockWeights[BB] << "\n";
                block_after_str += to_string(BB->getNumber()) + ":" + to_string(BlockWeights[BB]) + ",";
            }
        }
        // for (auto &I : BlockWeights) {
        //     outs() << I.first->getNumber() << ": " << I.second << "\n";
        //     block_after_str += to_string(I.first->getNumber()) + ":" + to_string(I.second) + ",";
        // }
        if (block_after_str.length() > 0)
            block_after_str.pop_back();

        row->appendData("\"" + block_before_str + "\"");
        row->appendData("\"" + block_after_str + "\"");
        row->appendData("\"" + feature_str + "\"");
        row->appendData("\"" + ajacent_str + "\"");
        sample_csv.appendRow(row);

        sample_csv.flush();

        outs() << "\nEdgeWeights after inference:\n";
        for (auto &I : EdgeWeights) {
            outs() << I.first.first->getNumber() << "->" << I.first.second->getNumber()
                << ": " << I.second << "\n";
        }
        outs() << "End of applying profi inference\n";
    

    }

private:
    FT& F; // reference to the function
    SampleProfileInference<FT> inference; // proxy target
};


} // namespace llvm

#endif