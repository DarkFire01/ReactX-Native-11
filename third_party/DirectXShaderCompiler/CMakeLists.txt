# DirectXShaderCompiler subset used by dxilconv, see README.md

set(DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

# Include paths and defines every user of the headers needs
add_library(dxc_headers INTERFACE)
target_include_directories(dxc_headers INTERFACE
    "${DXC_ROOT}/include/llvm/llvm_assert"
    "${DXC_ROOT}/generated/include"
    "${DXC_ROOT}/include"
    "${DXC_ROOT}/projects/include"
    "${DXC_ROOT}/generated/projects/include"
    "${DXC_ROOT}/projects/dxilconv/include")
target_compile_definitions(dxc_headers INTERFACE
    SUPPORT_QUERY_GIT_COMMIT_INFO
    _CRT_NONSTDC_NO_DEPRECATE
    _CRT_NONSTDC_NO_WARNINGS
    _CRT_SECURE_NO_DEPRECATE
    _CRT_SECURE_NO_WARNINGS
    _SCL_SECURE_NO_DEPRECATE
    _SCL_SECURE_NO_WARNINGS
    __STDC_CONSTANT_MACROS
    __STDC_FORMAT_MACROS
    __STDC_LIMIT_MACROS
    WIN32
    _WINDOWS)

# Upstream builds with exceptions and RTTI; only the vendored code gets these
add_library(dxc_flags INTERFACE)
target_compile_options(dxc_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/std:c++17>
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    $<$<COMPILE_LANGUAGE:CXX>:/GR>
    $<$<COMPILE_LANGUAGE:CXX>:/Zc:sizedDealloc->
    /W0
    /Gy
    /Zi)

function(dxc_add_library name dir)
    add_library(${name} STATIC EXCLUDE_FROM_ALL ${ARGN})
    target_include_directories(${name} PRIVATE "${DXC_ROOT}/${dir}")
    target_link_libraries(${name} PUBLIC dxc_headers PRIVATE dxc_flags)
    set_target_properties(${name} PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endfunction()

# LLVMSupport
dxc_add_library(dxc_support lib/Support
    lib/Support/APFloat.cpp
    lib/Support/APInt.cpp
    lib/Support/Atomic.cpp
    lib/Support/CommandLine.cpp
    lib/Support/ConvertUTF.c
    lib/Support/ConvertUTFWrapper.cpp
    lib/Support/Debug.cpp
    lib/Support/Dwarf.cpp
    lib/Support/ErrorHandling.cpp
    lib/Support/FoldingSet.cpp
    lib/Support/FormattedStream.cpp
    lib/Support/Hashing.cpp
    lib/Support/Host.cpp
    lib/Support/MD5.cpp
    lib/Support/ManagedStatic.cpp
    lib/Support/MemoryBuffer.cpp
    lib/Support/MemoryObject.cpp
    lib/Support/Mutex.cpp
    lib/Support/Path.cpp
    lib/Support/PrettyStackTrace.cpp
    lib/Support/Process.cpp
    lib/Support/Program.cpp
    lib/Support/RandomNumberGenerator.cpp
    lib/Support/Regex.cpp
    lib/Support/SmallPtrSet.cpp
    lib/Support/SmallVector.cpp
    lib/Support/SourceMgr.cpp
    lib/Support/StreamingMemoryObject.cpp
    lib/Support/StringExtras.cpp
    lib/Support/StringMap.cpp
    lib/Support/StringRef.cpp
    lib/Support/StringSaver.cpp
    lib/Support/TargetParser.cpp
    lib/Support/ThreadLocal.cpp
    lib/Support/Threading.cpp
    lib/Support/TimeProfiler.cpp
    lib/Support/TimeValue.cpp
    lib/Support/Timer.cpp
    lib/Support/Triple.cpp
    lib/Support/Twine.cpp
    lib/Support/raw_ostream.cpp
    lib/Support/regcomp.c
    lib/Support/regerror.c
    lib/Support/regexec.c
    lib/Support/regfree.c
    lib/Support/regmalloc.cpp
    lib/Support/regstrlcpy.c)

# LLVMMSSupport
dxc_add_library(dxc_mssupport lib/MSSupport
    lib/MSSupport/MSFileSystemImpl.cpp)

# LLVMDxcSupport
dxc_add_library(dxc_dxcsupport lib/DxcSupport
    lib/DxcSupport/FileIOHelper.cpp
    lib/DxcSupport/Global.cpp
    lib/DxcSupport/dxcmem.cpp)

# LLVMCore
dxc_add_library(dxc_core lib/IR
    lib/IR/AsmWriter.cpp
    lib/IR/Attributes.cpp
    lib/IR/AutoUpgrade.cpp
    lib/IR/BasicBlock.cpp
    lib/IR/Comdat.cpp
    lib/IR/ConstantFold.cpp
    lib/IR/ConstantRange.cpp
    lib/IR/Constants.cpp
    lib/IR/DIBuilder.cpp
    lib/IR/DataLayout.cpp
    lib/IR/DebugInfo.cpp
    lib/IR/DebugInfoMetadata.cpp
    lib/IR/DebugLoc.cpp
    lib/IR/DiagnosticInfo.cpp
    lib/IR/DiagnosticPrinter.cpp
    lib/IR/Dominators.cpp
    lib/IR/Function.cpp
    lib/IR/GVMaterializer.cpp
    lib/IR/Globals.cpp
    lib/IR/IRPrintingPasses.cpp
    lib/IR/InlineAsm.cpp
    lib/IR/Instruction.cpp
    lib/IR/Instructions.cpp
    lib/IR/IntrinsicInst.cpp
    lib/IR/LLVMContext.cpp
    lib/IR/LLVMContextImpl.cpp
    lib/IR/LegacyPassManager.cpp
    lib/IR/MDBuilder.cpp
    lib/IR/Metadata.cpp
    lib/IR/MetadataTracking.cpp
    lib/IR/Module.cpp
    lib/IR/Operator.cpp
    lib/IR/Pass.cpp
    lib/IR/PassRegistry.cpp
    lib/IR/Statepoint.cpp
    lib/IR/Type.cpp
    lib/IR/TypeFinder.cpp
    lib/IR/Use.cpp
    lib/IR/User.cpp
    lib/IR/Value.cpp
    lib/IR/ValueSymbolTable.cpp
    lib/IR/ValueTypes.cpp)

# LLVMBitReader
dxc_add_library(dxc_bitreader lib/Bitcode/Reader
    lib/Bitcode/Reader/BitcodeReader.cpp
    lib/Bitcode/Reader/BitstreamReader.cpp)

# LLVMBitWriter
dxc_add_library(dxc_bitwriter lib/Bitcode/Writer
    lib/Bitcode/Writer/BitcodeWriter.cpp
    lib/Bitcode/Writer/ValueEnumerator.cpp)

# LLVMAnalysis
dxc_add_library(dxc_analysis lib/Analysis
    lib/Analysis/AliasAnalysis.cpp
    lib/Analysis/AliasSetTracker.cpp
    lib/Analysis/AssumptionCache.cpp
    lib/Analysis/CFG.cpp
    lib/Analysis/CaptureTracking.cpp
    lib/Analysis/ConstantFolding.cpp
    lib/Analysis/DxilConstantFolding.cpp
    lib/Analysis/DxilConstantFoldingExt.cpp
    lib/Analysis/DxilSimplify.cpp
    lib/Analysis/InstructionSimplify.cpp
    lib/Analysis/IteratedDominanceFrontier.cpp
    lib/Analysis/LibCallSemantics.cpp
    lib/Analysis/LoopInfo.cpp
    lib/Analysis/MemoryBuiltins.cpp
    lib/Analysis/MemoryDependenceAnalysis.cpp
    lib/Analysis/MemoryLocation.cpp
    lib/Analysis/NoAliasAnalysis.cpp
    lib/Analysis/PHITransAddr.cpp
    lib/Analysis/ReducibilityAnalysis.cpp
    lib/Analysis/TargetLibraryInfo.cpp
    lib/Analysis/TypeBasedAliasAnalysis.cpp
    lib/Analysis/ValueTracking.cpp
    lib/Analysis/VectorUtils2.cpp)

# LLVMTransformUtils
dxc_add_library(dxc_transformutils lib/Transforms/Utils
    lib/Transforms/Utils/BasicBlockUtils.cpp
    lib/Transforms/Utils/BreakCriticalEdges.cpp
    lib/Transforms/Utils/CloneFunction.cpp
    lib/Transforms/Utils/CloneModule.cpp
    lib/Transforms/Utils/Local.cpp
    lib/Transforms/Utils/LoopSimplifyId.cpp
    lib/Transforms/Utils/LowerInvoke.cpp
    lib/Transforms/Utils/LowerSwitch.cpp
    lib/Transforms/Utils/Mem2Reg.cpp
    lib/Transforms/Utils/PromoteMemoryToRegister.cpp
    lib/Transforms/Utils/UnifyFunctionExitNodes.cpp
    lib/Transforms/Utils/ValueMapper.cpp)

# LLVMScalarOpts
dxc_add_library(dxc_scalaropts lib/Transforms/Scalar
    lib/Transforms/Scalar/DCE.cpp)

# LLVMDXIL
dxc_add_library(dxc_dxilcore lib/DXIL
    lib/DXIL/DxilCBuffer.cpp
    lib/DXIL/DxilCompType.cpp
    lib/DXIL/DxilCounters.cpp
    lib/DXIL/DxilInterpolationMode.cpp
    lib/DXIL/DxilMetadataHelper.cpp
    lib/DXIL/DxilModule.cpp
    lib/DXIL/DxilModuleHelper.cpp
    lib/DXIL/DxilNodeProps.cpp
    lib/DXIL/DxilOperations.cpp
    lib/DXIL/DxilResource.cpp
    lib/DXIL/DxilResourceBase.cpp
    lib/DXIL/DxilResourceBinding.cpp
    lib/DXIL/DxilResourceProperties.cpp
    lib/DXIL/DxilSampler.cpp
    lib/DXIL/DxilSemantic.cpp
    lib/DXIL/DxilShaderFlags.cpp
    lib/DXIL/DxilShaderModel.cpp
    lib/DXIL/DxilSignature.cpp
    lib/DXIL/DxilSignatureElement.cpp
    lib/DXIL/DxilSubobject.cpp
    lib/DXIL/DxilTypeSystem.cpp
    lib/DXIL/DxilUtil.cpp
    lib/DXIL/DxilUtilDbgInfoAndMisc.cpp)

# LLVMDxilContainer
dxc_add_library(dxc_dxilcontainer lib/DxilContainer
    lib/DxilContainer/DxilContainer.cpp
    lib/DxilContainer/DxilContainerAssembler.cpp
    lib/DxilContainer/DxilContainerReader.cpp
    lib/DxilContainer/DxilPipelineStateValidation.cpp
    lib/DxilContainer/DxilRDATBuilder.cpp)

# LLVMDxilRootSignature
dxc_add_library(dxc_dxilrootsignature lib/DxilRootSignature
    lib/DxilRootSignature/DxilRootSignature.cpp
    lib/DxilRootSignature/DxilRootSignatureConvert.cpp
    lib/DxilRootSignature/DxilRootSignatureSerializer.cpp
    lib/DxilRootSignature/DxilRootSignatureValidator.cpp)

# ShaderBinary
dxc_add_library(dxc_shaderbinary projects/dxilconv/lib/ShaderBinary
    projects/dxilconv/lib/ShaderBinary/ShaderBinary.cpp)

# DxilConvPasses
dxc_add_library(dxc_dxilconvpasses projects/dxilconv/lib/DxilConvPasses
    projects/dxilconv/lib/DxilConvPasses/DxilCleanup.cpp)

# DxbcConverter
dxc_add_library(dxc_dxbcconverter_impl projects/dxilconv/lib/DxbcConverter
    projects/dxilconv/lib/DxbcConverter/DxbcConverter.cpp
    projects/dxilconv/lib/DxbcConverter/DxbcUtil.cpp)

# LLVM 3.7 fork and the DXIL module, container and root signature code
add_library(dxc_dxil INTERFACE)
target_link_libraries(dxc_dxil INTERFACE
    dxc_support
    dxc_mssupport
    dxc_dxcsupport
    dxc_core
    dxc_bitreader
    dxc_bitwriter
    dxc_analysis
    dxc_transformutils
    dxc_scalaropts
    dxc_dxilcore
    dxc_dxilcontainer
    dxc_dxilrootsignature
    ole32
    advapi32)

# hlsl::DxbcConverter and the DXBC parser it uses
add_library(dxc_dxbcconverter INTERFACE)
target_link_libraries(dxc_dxbcconverter INTERFACE
    dxc_shaderbinary
    dxc_dxilconvpasses
    dxc_dxbcconverter_impl
    dxc_dxil)
