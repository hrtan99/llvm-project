add_llvm_component_library(LLVMProfileInference
    CSVUtil.cpp
    PGOInstrumentationPrint.cpp

    ADDITIONAL_HEADER_DIRS
    ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/ProfileInference

    COMPONENT_NAME
    ProfileInference
)