#include "llvm/Transforms/ProfileInference/CSVUtil.h"

using namespace llvm;

CSV* CSV::instance = nullptr;
std::mutex mtx;