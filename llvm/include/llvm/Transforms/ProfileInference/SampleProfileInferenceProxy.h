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

        // skip if the function contains only one basic block or no basic block
        if (F.size() == 0 || F.size() == 1) {
            outs() << "Skip Function with " << F.size() << " BB: " << F.getName() << "\n";
            return;
        }

        using namespace std;  
        shared_ptr<Row> headLine = make_shared<Row>();
        (*headLine)
            .appendData("func")
            .appendData("block_freq_before")
            .appendData("block_freq_after")
            ;

        pid_t processID = getpid(); // 获取当前进程 ID
        // std::string name = PGOBenchmarkName + "_" + to_string(processID) + ".sample.csv";
        std::string fileName = SampleDataFileName + "." + to_string(processID);
        bool isExist = CSV::create(fileName);
        CSV& sample_csv = CSV::getRef();
        // if (!isExist) {
        //     sample_csv.setHead(headLine);
        // }

        shared_ptr<Row> row = make_shared<Row>();
        row->appendData(F.getName().str());

        outs() << "\nApplying profi inference\n";
        outs() << "\nFunction: " << F.getName() << "\n";

        std::string block_before_str = "";
        // Only execute this part if BasicBlockT is of type BasicBlock
        if constexpr (std::is_same_v<BasicBlockT, llvm::BasicBlock>) {
            std::vector<BasicBlock*> topoOrder = getTopologicalOrder();
            for (BasicBlock* BB : topoOrder) {
                outs() << BB->getNumber() << ": " << BlockWeights[BB] << "\n";
                block_before_str += to_string(BB->getNumber()) + ":" + to_string(BlockWeights[BB]) + ",";
            }
        }
        else {
            outs() << "BasicBlockT is not of type BasicBlock\n";
        }
        if (block_before_str.length() > 0)
            block_before_str.pop_back();

        // for (auto &I : BlockWeights) {
        //     outs() << I.first->getNumber() << ": " << I.second << "\n";
        //     int blockNumber = I.first->getNumber();
        //     block_before_str += to_string(I.first->getNumber()) + ":" + to_string(I.second) + ",";
        // }


        outs() << "\nBlockWeights before inference:\n";
        outs() << block_before_str << "\n";

        outs() << "\nEdgeWeights before inference:\n";
        for (auto &I : EdgeWeights) {
            outs() << I.first.first->getNumber() << "->" << I.first.second->getNumber()
                << ": " << I.second << "\n";
        }

        int totalExcutionCount = 0;
        for (auto &I : BlockWeights) {
            totalExcutionCount += I.second;
        }
        
        // record start time
        auto start = std::chrono::high_resolution_clock::now();

        int blockWeightSizeBefore = BlockWeights.size(), BBSizeBefore = F.size();
        inference.apply(BlockWeights, EdgeWeights);
        int blockWeightSizeAfter = BlockWeights.size(), BBSizeAfter = F.size();

        // record end time
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        outs() << "Inference time: " << duration.count() << " microseconds\n";
        
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

        if (block_after_str.length() > 0)
            block_after_str.pop_back();

        row->appendData("\"" + block_before_str + "\"");
        row->appendData("\"" + block_after_str + "\"");
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