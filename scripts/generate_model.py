import sys
import json

class function():
    def __init__(self, pe, func, args):
        self.func_signature = func
        self.token = args["token"].split(' ')[0]
        print("TOKEN: " + self.token)
        self.trace_func_llvm_name = pe.name + func.split('(')[0]
        self.trace_func = "_C_" + self.trace_func_llvm_name
        self.runtime_func = func
        self.cycles = args["cycles"]
        if 'load_penalty' in args:
            self.load_penalty = args["load_penalty"]
        else:
            self.load_penalty = 0
        if 'store_penalty' in args:
            self.store_penalty = args["store_penalty"]
        else:
            self.store_penalty = 0

        #if self.func_signature.count(',') == 0:
        #    if len(self.func_signature[self.func_signature.find('('):self.func_signature.find(')')]) > 1:
        #        self.num_args = 1
        #    else:
        #        self.num_args = 0
        #else:
        #    self.num_args = self.func_signature.count(',') + 1
        self.num_args = args["token"].count('#')
        self.args = self.func_signature.split("(")[1:][0].split(")")[0].split(",")

class processing_element():
    def __init__(self, name):
        self.ids = []
        self.num_elems = len(self.ids)
        self.functions = []
        self.name = name

    def add_func(self, func, args):
        self.functions.append(function(self, func, args))

def parse(json):
    addr_list = []
    pe_list = []

    for entry in json["addr_space"]:
        pair = (json["addr_space"][entry]['start'],json["addr_space"][entry]['end'])
        addr_list.append(pair)

    for pe in json["pe"]:
        PE = processing_element(pe)
        PE.ids.append(json["pe"][pe]["id"])
        for entry in json["pe"][pe]:
            if entry != "id":
                if json["pe"][pe][entry]["enable"] == 1:
                    PE.add_func(entry, json["pe"][pe][entry])
        pe_list.append(PE)

    llvm_instr = json["llvm_instr"]
    return llvm_instr,addr_list,pe_list

def generate_util():
    with open("../tracer/compiler-pass/util.hpp", "w") as f:
        sys.stdout = f

        util = """
#ifndef HETSIM_UTIL_HPP_
#define HETSIM_UTIL_HPP_

#include <cxxabi.h>

inline std::string demangle(const char *name) {
  int status = -1;

  std::unique_ptr<char, void (*)(void *)> res{
      abi::__cxa_demangle(name, NULL, NULL, &status), std::free};
  return (status == 0) ? res.get() : std::string(name);
}

static llvm::Function *getFunctionFromInst(llvm::Instruction &I,
                                           std::string s) {
  llvm::BasicBlock *B = I.getParent();
  llvm::Function *F = B->getParent();
  llvm::Module *M = F->getParent();
  llvm::Function *R = M->getFunction(s);
  return R;
}

#endif /* HETSIM_UTIL_HPP_ */
"""
        print(util)

def generate(pes):
    includes = '''
    // LLVM
    #include "llvm/IR/Function.h"
    #include "llvm/Pass.h"
    #include "llvm/Support/raw_ostream.h"

    #include "llvm/IR/BasicBlock.h"
    #include "llvm/IR/Instruction.h"

    #include "llvm/Analysis/DependenceAnalysis.h"
    #include "llvm/Analysis/MemoryBuiltins.h"
    #include "llvm/Analysis/MemorySSA.h"

    #include "llvm/ADT/SmallVector.h"

    #include "llvm/IR/IRBuilder.h"
    #include "llvm/IR/Type.h"

    #include "llvm/Analysis/DDG.h"

    #include "llvm/IR/LegacyPassManager.h"
    #include "llvm/Transforms/IPO/PassManagerBuilder.h"

    // Used to identify builtin functions and ignore them
    #include "llvm/Analysis/TargetLibraryInfo.h"

    // Used for injecting if-then blocks
    #include "llvm/Transforms/Utils/BasicBlockUtils.h"

    // standard
    #include <vector>

    // project
    #include "hetsim-analysis.hpp"
    #include "util.hpp"

    #define DEBUG 0

    namespace {

    struct HetsimRuntime {
    static constexpr char *EmitLoad = "emit_load";
    static constexpr char *EmitStall = "emit_stall";
    static constexpr char *EmitMalloc = "emit_malloc";
    static constexpr char *EmitIsLogOpen = "is_log_open";
    static constexpr char *EmitIncrementStalls = "increment_stalls";
    static constexpr char *EmitStore = "emit_store";
    '''

    original_stdout = sys.stdout

    with open("../tracer/compiler-pass/hetsim-analysis.cpp", "w") as f:
        sys.stdout = f
        print(includes)

        # Declare Runtime Functions
        for pe in pes:
            for f in pe.functions:
                print("static constexpr char *" + f.trace_func_llvm_name + " = " + "\"" + f.trace_func + "\";\n")

        print("static const std::vector<std::string> Functions;\n")
        print("};\n")

        print("const std::vector<std::string> HetsimRuntime::Functions = {\n")

        for pe in pes:
            for f in pe.functions:
                print("HetsimRuntime::" + f.trace_func_llvm_name + ",\n")

        print("""HetsimRuntime::EmitLoad,
HetsimRuntime::EmitStore,
HetsimRuntime::EmitStall,
HetsimRuntime::EmitIncrementStalls,
HetsimRuntime::EmitIsLogOpen,
HetsimRuntime::EmitMalloc""")
        print("};\n")

        # Map from runtime function to tracing function
        print("std::map<std::string,std::string> intrinsic_trace_map = {\n")
        for pe in pes:
            for f in pe.functions:
                print("std::pair<std::string, std::string>(std::string(\"" + f.runtime_func + "\"), std::string(\"" + f.trace_func + "\")),\n")

        print("};\n")

        print("} // namespace\n")


        declareRuntime = ''' void HetsimPass::declareRuntime(Module &Mod) {
            for (std::string e : HetsimRuntime::Functions) {
        #if DEBUG == 1
            fprintf(stderr, "Registering: %s\\n", e.c_str());
        #endif

        auto *funcType = llvm::FunctionType::get(
                llvm::Type::getInt32Ty(Mod.getContext()), true);

        Mod.getOrInsertFunction(e, funcType);
        }
        return;
        } '''

        print(declareRuntime)

        isFunctionBlacklisted = '''

        bool HetsimPass::isFunctionBlacklisted(std::string func_name,
        std::set<std::string> builtins) {
        return false;
        for (std::string e : HetsimRuntime::Functions) {

        if (e == demangle(func_name.c_str())
                || demangle(func_name.c_str()).find("llvm")
                != std::string::npos
                || demangle(func_name.c_str()).find("operator")
                != std::string::npos
                || demangle(func_name.c_str()).find("main")
                != std::string::npos) {
            printf("Blacklisted: %s\\n", func_name.c_str());
            return true;
        }
        }

        for (std::string e : HetsimRuntime::Functions) {
        #if DEBUG == 1
        printf("isFuncBlacklisted: |%s| |%s|\\n", e.c_str(), demangle(func_name.c_str()).c_str());
        #endif
        if (e == demangle(func_name.c_str())) {
        return true;
        }
    }

        for (std::string s : builtins) {
        #if DEBUG == 1
        printf("isFuncBlacklisted: |%s| |%s|\\n", s.c_str(), func_name.c_str());
    #endif
        if (builtins.count(func_name) != 0) {
        return true;
        }
        }

        if (intrinsic_trace_map.find(func_name) != intrinsic_trace_map.end() ) {
        return true;
        }

        return false;
        }
        '''

        print(isFunctionBlacklisted)

        getAnalysisUsage = '''
        void HetsimPass::getAnalysisUsage(AnalysisUsage &AU) const {
        AU.setPreservesAll();
        AU.addRequired<DependenceAnalysisWrapperPass>();
        }
        '''

        print(getAnalysisUsage)

        identify_load_store = '''

void HetsimPass::identify(llvm::Module &M,
        std::vector<Load> &load_instructions,
        std::vector<Store> &store_instructions,
        std::vector<Stall> &stalls,
        std::vector<DepStall> &dep_stalls,
        std::vector<Intrinsic> &intrinsics) {

    int emit_count = 0;
    int mem_access_id = 0;

#if DEBUG == 1
    fprintf(stderr, "Identify\\n");
#endif

    // Constant Global Definitions

    for (llvm::Function &F : M) {
        if (!isFunctionBlacklisted(demangle(F.getName().str().c_str()), builtins)) {
#if DEBUG == 1
            llvm::errs() << "Analyzing function: " << demangle(F.getName().str().c_str()) << "\\n";
#endif
            for (llvm::BasicBlock &BB : F) {
                int stall_count = 0;

                std::vector<llvm::Instruction*> seen_list;

                for (llvm::Instruction &I : BB) {
                    seen_list.push_back(&I);

                    if (I.getOpcode() == llvm::Instruction::Load) {
                        llvm::SmallVector<llvm::Instruction*,3> deps;
                        int dep_count = 0;

                        if (!F.isDeclaration()) {
                            DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>(F).getDI();
                            DataDependenceGraph DDG(F, DI);

                            for (DataDependenceGraph::iterator node = DDG.begin(), e = DDG.end(); node != e; ++node) {
                                if (auto *simpleNode = dyn_cast<SimpleDDGNode>(*node)) {
                                    for (DDGNode::iterator edge = (*node)->begin(), e = (*node)->end(); edge != e; ++edge) {
                                        if ((*edge)->isMemoryDependence()) {
                                            auto *targNode = dyn_cast<SimpleDDGNode>( &((*edge)->getTargetNode()));
                                            //                                            if (dyn_cast<SimpleDDGNode>(*node)->getFirstInstruction()->getOpcode() == llvm::Instruction::Load &&
                                            //                                                    targNode->getFirstInstruction()->getOpcode() == llvm::Instruction::Load) {
                                            if (dep_count < 3 && simpleNode->getFirstInstruction() == &I &&
                                                    targNode->getFirstInstruction()->getParent() == I.getParent() &&
                                                    std::find(seen_list.begin(), seen_list.end(), targNode->getFirstInstruction()) != seen_list.end() &&
                                                    // TODO: CHECK THAT INSTRUCTION DOMINATES ALL USES
                                                    (targNode->getFirstInstruction()->getOpcode() == llvm::Instruction::Load ||
                                                            targNode->getFirstInstruction()->getOpcode() == llvm::Instruction::Store)) {
                                                deps.push_back(targNode->getFirstInstruction());
#if DEBUG == 1
                                                errs() << "DDG INFO: " << *(dyn_cast<SimpleDDGNode>(*node)->getFirstInstruction()) << " " << *(targNode->getFirstInstruction()) << "\\n";
#endif
                                                dep_count += 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }

#if DEBUG == 1
                         errs() << "DDG identified\\n";
#endif
                        emit_count++;
                        llvm::SmallVector<llvm::Value*,5> args;
                        args.push_back(I.getOperand(0));
                        args.push_back(llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), mem_access_id++));

#if DEBUG == 1
                         errs() << "DDG base\\n";
#endif

                        args.push_back((llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), dep_count)));

                        for (auto arg : deps) {
#if DEBUG == 1
                            errs() << "DDG Iterate deps\\n";
#endif
                            if (arg->getOpcode() == llvm::Instruction::Load) {
#if DEBUG == 1
                                errs() << "DDG push back load\\n";
#endif
                                args.push_back(arg->getOperand(0));
                            }
                            else if (arg->getOpcode() == llvm::Instruction::Store) {
#if DEBUG == 1
                                errs() << "DDG push back store\\n";
#endif
                                args.push_back(arg->getOperand(1));
                            }
                            else {
#if DEBUG == 1
                                errs() << "DDG What instruction is this? \\n";
#endif
                            }
                        }

#if DEBUG == 1
                        errs() << "DDG Push back args\\n";
#endif

                        load_instructions.push_back({&I,HetsimRuntime::EmitLoad,args}); // @4

#if DEBUG == 1
                        errs() << "DDG Args pushed back";
#endif

                        if (stall_count > 0) {
                            stalls.push_back({&I,stall_count});
                            stall_count = 0;
                        }
                    }
                    else if (I.getOpcode() == llvm::Instruction::Store) {

                        llvm::SmallVector<llvm::Instruction*,3> deps;
                        int dep_count = 0;

                        if (!F.isDeclaration()) {
                            DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>(F).getDI();
                            DataDependenceGraph DDG(F, DI);

                            for (DataDependenceGraph::iterator node = DDG.begin(), e = DDG.end(); node != e; ++node) {
                                if (auto *simpleNode = dyn_cast<SimpleDDGNode>(*node)) {
                                    for (DDGNode::iterator edge = (*node)->begin(), e = (*node)->end(); edge != e; ++edge) {
                                        if ((*edge)->isMemoryDependence()) {
                                            auto *targNode = dyn_cast<SimpleDDGNode>( &((*edge)->getTargetNode()));
                                            if (dep_count < 3 && simpleNode->getFirstInstruction() == &I &&
                                                    targNode->getFirstInstruction()->getParent() == I.getParent() &&
                                                    std::find(seen_list.begin(), seen_list.end(), targNode->getFirstInstruction()) != seen_list.end() &&
                                                    // TODO: CHECK THAT INSTRUCTION DOMINATES ALL USES
                                                    (targNode->getFirstInstruction()->getOpcode() == llvm::Instruction::Load ||
                                                            targNode->getFirstInstruction()->getOpcode() == llvm::Instruction::Store)) {
                                                deps.push_back(targNode->getFirstInstruction());
#if DEBUG == 1
                                                errs() << "DDG INFO: " << *(dyn_cast<SimpleDDGNode>(*node)->getFirstInstruction()) << " " << *(targNode->getFirstInstruction()) << "\\n";
#endif
                                                dep_count += 1;
                                            }
                                        }
                                    }
                                }
                            }
                        }


#if DEBUG == 1
                        errs() << "DDG identified\\n";
#endif

                        emit_count++;
                        llvm::SmallVector<llvm::Value*,7> args;
                        args.push_back(I.getOperand(1));
                        args.push_back(I.getOperand(0));
                        args.push_back(llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), mem_access_id++));

#if DEBUG == 1
                        errs() << "DDG base\\n";
#endif

                        args.push_back((llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), dep_count)));

                        for (auto arg : deps) {
                            if (arg->getOpcode() == llvm::Instruction::Load) {
#if DEBUG == 1
                                errs() << "DDG S push back load\\n";
#endif
                                args.push_back(arg->getOperand(0));
                            }
                            else if (arg->getOpcode() == llvm::Instruction::Store) {
#if DEBUG == 1
                                errs() << "DDG S push back store\\n";
#endif
                                args.push_back(arg->getOperand(1));
                            }
                            else {
#if DEBUG == 1
                                 errs() << "DDG S What instruction is this? \\n";
#endif
                            }
                        }

                        store_instructions.push_back({&I,HetsimRuntime::EmitStore,args});

                        if (stall_count > 0) {
                            stalls.push_back({&I,stall_count});
                            stall_count = 0;
                        }
                    }

        '''

        print(identify_load_store)

        identify_call = '''
                    else if (I.getOpcode() == llvm::Instruction::Call) {
                        llvm::Function * called_func = cast<CallInst>(I).getCalledFunction();
                        std::string maybe_mangled_name;
                        std::string func_name;
                        if (called_func != NULL) {
                            maybe_mangled_name = called_func->getName().str();
                        }
                        if (maybe_mangled_name.substr(0, 2) == "_Z") {
                            func_name = demangle(maybe_mangled_name.c_str());
                        }
        '''
        count = 0
        for pe in pes:
            for f in pe.functions:
                if count == 0:
                    identify_call += '\nif (std::string(\"' + f.func_signature + '''\") == func_name) {
                                emit_count++;
                                llvm::SmallVector<llvm::Value*,2> args;\nintrinsics.push_back({&I,std::string("''' + f.func_signature + '"),args});\n}'''
                else:
                    identify_call += '\nelse if (std::string(\"' + f.func_signature + '''\") == func_name) {
                                emit_count++;
                                llvm::SmallVector<llvm::Value*,2> args;\nintrinsics.push_back({&I,std::string("''' + f.func_signature + '"),args});\n}'''
                count += 1

        identify_call += "}\n"
        identify_call += '''
                    else if (!(I.isUsedByMetadata()) && I.getOpcode()) {
                        if ('''
        count = 1
        for i in llvm_instr:
            if count == len(llvm_instr):
                identify_call += "I.getOpcode() == llvm::Instruction::" + i
            else:
                identify_call += "I.getOpcode() == llvm::Instruction::" + i + " ||\n"

            count += 1
        identify_call += '''){\n

#if DEBUG == 1
                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif

                            llvm::SmallVector<llvm::Instruction*,3> deps;
                            int dep_count = 0;

#if DEBUG == 1
                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif

                            DependenceInfo &DI = getAnalysis<DependenceAnalysisWrapperPass>(F).getDI();
                            DataDependenceGraph DDG(F, DI);

#if DEBUG == 1
                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif

                            for (DataDependenceGraph::iterator node = DDG.begin(); node != DDG.end(); ++node) {
#if DEBUG == 1
                                errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                if (SimpleDDGNode *simpleNode = dyn_cast<SimpleDDGNode>(*node)) {
#if DEBUG == 1
                                    errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                    for (DDGNode::iterator edge = (*node)->begin(); edge != (*node)->end(); ++edge) {
#if DEBUG == 1
                                        errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                        if (SimpleDDGNode *targNode = dyn_cast<SimpleDDGNode>( &((*edge)->getTargetNode()))) {
#if DEBUG == 1
                                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                            for (llvm::SmallVectorImpl<llvm::Instruction*>::iterator DI = simpleNode->getInstructions().begin(); DI != simpleNode->getInstructions().end(); ++DI) {
#if DEBUG == 1
                                                errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                                for (llvm::SmallVectorImpl<llvm::Instruction*>::iterator CI = targNode->getInstructions().begin(); CI != targNode->getInstructions().end(); ++CI) {
#if DEBUG == 1
                                                    errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                                    if (dep_count < 3 && *CI == &I) {
#if DEBUG == 1
                                                        errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
                                                        errs() << "DDG STALL INFO ALL: " << **CI << " " << **DI <<
                                                                " " << (std::find(seen_list.begin(), seen_list.end(), *DI) != seen_list.end()) <<
                                                                " " << ((*DI)->getOpcode() == llvm::Instruction::Load) <<
                                                                " " << ((*DI)->getOpcode() == llvm::Instruction::Store) << "\\n";
#endif
                                                        if ((((*DI)->getOpcode() == llvm::Instruction::Load) || ((*DI)->getOpcode() == llvm::Instruction::Store)) &&
                                                                std::find(seen_list.begin(), seen_list.end(), *DI) != seen_list.end()) {
#if DEBUG == 1
                                                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                                            deps.push_back(*DI);
#if DEBUG == 1
                                                            errs() << "DDG STALL INFOO: " << **CI << " " << **DI << "\\n";
#endif
                                                            dep_count += 1;
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

#if DEBUG == 1
                            errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif

                            ++stall_count;
                            emit_count++;

                            if (dep_count > 0) {
#if DEBUG == 1
                                errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                llvm::SmallVector<llvm::Value*,5> args;

                                args.push_back((llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), stall_count)));
                                stall_count = 0;

                                args.push_back((llvm::ConstantInt::get( llvm::IntegerType::get(M.getContext(), 32), dep_count)));

                                for (auto arg : deps) {
#if DEBUG == 1
                                    errs() << __FILE__ << " " << __FUNCTION__ << " " << __LINE__ << "\\n";
#endif
                                    if (arg->getOpcode() == llvm::Instruction::Load) {
#if DEBUG == 1
                                        errs() << "DDG S push back load\\n";
#endif
                                        args.push_back(arg->getOperand(0));
                                    }
                                    else if (arg->getOpcode() == llvm::Instruction::Store) {
#if DEBUG == 1
                                        errs() << "DDG S push back store\\n";
#endif
                                        args.push_back(arg->getOperand(1));
                                    }
                                    else {
#if DEBUG == 1
                                        errs() << "DDG S What instruction is this? \\n";
#endif
                                    }
                                }

                                dep_stalls.push_back({&I,args});
                            }

                        }
                    }
                }

                if (stall_count > 0) {
                    emit_count ++;
                    stalls.push_back({BB.getTerminator(),stall_count});
                    stall_count = 0;
                }
            }
        }

        if (emit_count > 0) {
#if DEBUG == 1
            llvm::errs() << "emitted " << demangle(F.getName().str().c_str()) << "\\n";
#endif
            emit_count = 0;
        }
    }
}
        '''

        print(identify_call)

        run_on_module = """
bool HetsimPass::runOnModule(llvm::Module &M) {

    declareRuntime(M);

    const TargetLibraryInfo *TLI;
    LibFunc inbuilt_func;

    /*for (llvm::Function &F : M) {
        fprintf(stderr, "runOnModule 1a\\n");
        if (TLI->getLibFunc(F, inbuilt_func))
            fprintf(stderr, "runOnModule 1 %s\\n", F.getFunction().getName().str().c_str());
        builtins.insert(F.getFunction().getName().str());
    }*/

    int counter = 0;

    std::vector<Load> load_instructions;
    std::vector<Store> store_instructions;
    std::vector<Stall> stalls;
    std::vector<DepStall> dep_stalls;
    std::vector<Intrinsic> intrinsics;

    identify(M, load_instructions, store_instructions, stalls, dep_stalls, intrinsics);

    for (auto L : load_instructions) {
        if (auto *func = M.getFunction(HetsimRuntime::EmitLoad)) { // @5
            auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), L.args, "", L.instr);
        }
    }

    for (auto S : store_instructions) {
        if (auto *func = M.getFunction(HetsimRuntime::EmitStore)) {
            auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), S.args, "", S.instr);
        }
    }

    for (auto S : stalls) {
        if (auto *func = M.getFunction(HetsimRuntime::EmitIncrementStalls)) {
            llvm::SmallVector<llvm::Value *, 2> args;
            args.push_back(llvm::ConstantInt::get(llvm::IntegerType::get(M.getContext(), 32), S.stall_count)); // number of stalls preceding this load
            args.push_back(llvm::ConstantInt::get(llvm::IntegerType::get(M.getContext(), 32), 0));
            auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), args, "", S.instr);
        }
    }

    for (auto S : dep_stalls) {
        if (auto *func = M.getFunction(HetsimRuntime::EmitIncrementStalls)) {
            auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func), S.args, "", S.instr);
#if DEBUG == 1
            errs() << "DDG Create " << S.args.size() << "\\n";
#endif
        }
    }

    for (auto I : intrinsics) {
    """
        print(run_on_module)

        count = 0
        for pe in pes:
            for f in pe.functions:
                if count == 0:
                    case = """ if (I.trace_func == std::string(\""""
                else:
                    case = """ else if (I.trace_func == std::string(\""""
                count += 1
                case += f.func_signature
                case += """\")) {
                if (auto *func = M.getFunction(intrinsic_trace_map[I.trace_func])) {
                llvm::SmallVector<llvm::Value*,"""
                case += str(f.num_args)
                case += "> args;\n"
                for i in range(f.num_args):
                    case += "args.push_back(I.instr->getOperand(" + str(i) + "));\n"
                case += "auto *call = llvm::CallInst::Create(llvm::cast<llvm::Function>(func),args,\"\",I.instr);"
                case += """
                }
                else {
                    fprintf(stderr, "Can't find %s\\n", I.trace_func.c_str());
                    exit(0);
                }
            }
            """
            #else {
            #  fprintf(stderr, \"TODO %s %s\\n\", I.trace_func.c_str(),intrinsic_trace_map[I.trace_func].c_str());
            #}
            #"""
                print(case)
        run_on_module = """    }

    return false;
}
    """
        print(run_on_module)

        register  = """
char HetsimPass::ID = 0;

static llvm::RegisterPass<HetsimPass> X("hetsim-analysis",
        "Hetsim Compiler Pass", false, false);

static void registerHetsimPass(const llvm::PassManagerBuilder &Builder,
        llvm::legacy::PassManagerBase &PM) {
    PM.add(new HetsimPass());

    return;
}
static llvm::RegisterStandardPasses RegisterHetsimPass(
        llvm::PassManagerBuilder::EP_OptimizerLast, registerHetsimPass);
"""
        print(register)


def generate_header(pes):
    original_stdout = sys.stdout

    with open("../tracer/compiler-pass/hetsim-analysis.hpp", "w") as f:
        sys.stdout = f

        header = """
#ifndef HETSIM_HPP_
#define HETSIM_HPP_

// LLVM
#include "llvm/InitializePasses.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"

#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

#include "llvm/IR/BasicBlock.h"
// #include "llvm/IR/CallSite.h"
#include "llvm/IR/Instruction.h"

#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/TargetLibraryInfo.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

#include "llvm/ADT/SmallVector.h"
// using llvm::SmallVector

#include "llvm/Support/Debug.h"
// using DEBUG macro
// using llvm::dbgs

// standard
#include <vector>
// using std::vector

#include <string>
// using std::string

// project

//#include "../hetsim-compiler-pass/util.hpp"

#define DEBUG_TYPE "prefetcher-analysis"

namespace llvm {
class Value;
class Instruction;
}
;
// namespace llvm

struct MemInfo {
    llvm::Value *mem_acc;
};

struct HetsimAnalysisResult {
    llvm::SmallVector<MemInfo, 8> mem_accesses;
};

using namespace llvm;

// ***** helper function to print vectors ****** //
// This version of the function takes a vector of T* as input
template<typename T> void printVector(std::string inStr, T begin, T end) {
    errs() << inStr << ": < ";
    for (auto it = begin; it != end; ++it) {
        errs() << **it << " ";
    }
    errs() << ">";\n
}

class HetsimPass: public llvm::ModulePass {
public:
    static char ID;

    using ResultT = HetsimAnalysisResult;

    ResultT *Result;

    std::set<std::string> builtins;

    HetsimPass() :
            llvm::ModulePass(ID) {
        PassRegistry &Registry = *PassRegistry::getPassRegistry();
        initializeCore(Registry);
        initializeScalarOpts(Registry);
        initializeIPO(Registry);
        initializeAnalysis(Registry);
        initializeTransformUtils(Registry);
        initializeInstCombine(Registry);
        initializeInstrumentation(Registry);
        initializeTarget(Registry);
    }

    struct Store {
        llvm::Instruction *instr;
        std::string trace_func;
        llvm::SmallVector<llvm::Value *,7> args;
    };

    struct Load {
        llvm::Instruction *instr;
        std::string trace_func;
        llvm::SmallVector<llvm::Value *,5> args;
    };

    struct Intrinsic {
        llvm::Instruction *instr;
        std::string trace_func;
        llvm::SmallVector<llvm::Value *,2> args;
    };

    struct Stall {
        llvm::Instruction *instr;
        int stall_count;
    };

    struct DepStall {
        llvm::Instruction *instr;
        llvm::SmallVector<llvm::Value*,5> args;
    };


    void identify(llvm::Module &M,
            std::vector<Load> &load_instructions,
            std::vector<Store> &store_instructions,
            std::vector<Stall> &stalls,
            std::vector<DepStall> &dep_stalls,
            std::vector<Intrinsic> &intrinsics);

    void identifyLoads(llvm::Module &M, llvm::SmallVectorImpl<llvm::Instruction> &load_instructions);
    void identifyStores(llvm::Module &M, llvm::SmallVectorImpl<llvm::Instruction>& store_instructions);
    void identifyIntrinsics(llvm::Module &M, llvm::SmallVectorImpl<Intrinsic>& instrinsics);
    void emitLoadTrace(llvm::Module &M);
    void emitStoreTrace(llvm::Module &M);
    void emitIntrinsicTrace(llvm::Module &M);

    virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;

    llvm::Instruction* insertOpenLogCheck(llvm::Function *F, llvm::Instruction *InsertBefore);

    bool runOnModule(llvm::Module &M) override;

    void declareRuntime(Module &M);

    void createAndDestroyLogs(Module &M);

    bool isFunctionBlacklisted(std::string func_name,
            std::set<std::string> builtins);
    bool isFunctionWhitelisted(std::string func_name);
    std::vector<std::pair<llvm::Instruction*, llvm::Instruction*>> deps;
};

#endif // HETSIM_HPP_
"""
        print(header)

def generate_runtime(llvm_instr,addr_list,pes):
    original_stdout = sys.stdout

    with open("../tracer/runtime/default/hetsim_default_rt.cpp", "w") as f:
        sys.stdout = f

        includes = """

#include <stdio.h>
// using fprintf

#include <iostream>
#include <fstream>
#include <sstream>
// IO

#include <thread>
// thread id

#include <assert.h>

#include <vector>
#include <map>

#include "math.h"

#include "hetsim_default_rt.h"
#include "util.h"

#include <mutex>
#include <sys/stat.h>
    """
        print(includes)

        def_mutex = """
std::mutex log_mutex;
std::mutex stall_mutex;\n"""

        for pe in pes:
            def_mutex += "std::mutex " + pe.name + "_mutex;\n"

        print(def_mutex)

        def_maps = """static std::map<std::thread::id, bool> trace_bools;
static std::map<std::thread::id, int> stall_counts;\n"""

        #for pe in pes:
        #    def_maps += """static std::map<std::thread::id,bool> is_""" + pe.name + ";\n"
        def_maps += "std::map<unsigned, std::shared_ptr<std::ofstream>> traceFiles;\n";

        print(def_maps)

        addr_spaces = '''std::vector<std::pair<int,int>> addr_spaces = {'''
        for entry in addr_list:
            addr_spaces += 'std::pair<int,int>(' + entry[0] + ',' + entry[1] + ')'
        addr_spaces += '};'

        print(addr_spaces)

        for pe in pes:
            get_type = 'bool is_' + pe.name + '(int tid)\n'
            get_type += '{\n  switch(tid) {\n'
            for i in pe.ids:
                for j in i:
                    get_type += '    case ' + str(j) + ':\n'
            get_type += '    {\n'
            get_type += '      return true;\n'
            get_type += '      break;\n'
            get_type += '    }\n'
            get_type += '    default:\n'
            get_type += '    {\n'
            get_type += '      return false;\n'
            get_type += '      break;\n'
            get_type += '    }\n'
            get_type += '  }\n'
            get_type += '}\n'
            print(get_type)
        shouldTranslate = '''
std::string toHex(void *x) {
  std::stringstream sstream;
  sstream << std::hex << x;
  return sstream.str();
}

void *checkBoundsAndTranslate(void *addr) {

  for (auto a : addr_spaces) {
    if ((addr >= (void*)a.first) && addr <= (void*)a.second) {
      return addr;
    }
  }
  return nullptr;
}

bool shouldTranslate(void *addr) {
  for (auto a : addr_spaces) {
    if ((addr >= (void*)a.first) && addr <= (void*)a.second) {
      return true;
    }
  }
  return false;
}
'''

        print(shouldTranslate)

        #init_memtest = '''void init_hetsim() {\n'''

        #for pe in pes:
        #    init_memtest += "  for (int i = 0; i < " + str(pe.num_elems) + "; ++i) {\n"
        #    init_memtest += """    std::string fname = "traces/""" + pe.name + "_\" << std::to_string(i) << \".txt\";\n"
        #    init_memtest += "    " + pe.name + "traceFiles.insert(std::make_pair(i,std::make_shared<std::ofstream>(fname, std::ios::out)));\n"
        #    init_memtest += "  }\n"

        #init_memtest += "}\n"

        #print(init_memtest)

        register = "inline void __register_entry(unsigned id, std::string e) {\n"
        register += "  log_mutex.lock();\n"
        register += "  (*traceFiles.at(id)) << e;\n"
        register += "  traceFiles.at(id)->flush();\n"
        register += "  log_mutex.unlock();\n"
        register += "}"
        print(register)

        write_memtest = """void write_hetsim() {\n"""
        write_memtest += "  log_mutex.lock();\n"
        write_memtest += "  int size = traceFiles.size();\n"
        write_memtest += "  for (int i = 0; i < size; ++i) {\n"
        write_memtest += "    __register_entry(i, \"EOF\");\n"""
        write_memtest += "    traceFiles.at(i)->flush();\n"
        write_memtest += "    traceFiles.at(i)->close();\n"
        write_memtest += "  }\n"
        write_memtest += "  log_mutex.unlock();\n"
        write_memtest += "}\n"
        print(write_memtest)

        print("""

std::string __LD(void *addr, std::vector<void *> deps,
		bool blocking, int pc) {
	std::string e;

    e.append("LD");
    addr = checkBoundsAndTranslate(addr);
    if (addr != nullptr) {
        if (blocking)
            e.append("B");

        if (pc != -1) {
            e.append(std::string(" @"));
            e.append(std::to_string(pc));
            e.append(std::string(" "));
        }

        e.append(toHex(addr));
        e.append(" ( ");
        for (auto dep : deps) {
            e.append(toHex(checkBoundsAndTranslate(dep)));
            e.append(" ");
        }
        e.append(" )\\n");
    }
    return e;
}

std::string __ST(void *addr, uint64_t data,
		std::vector<void *> deps, bool blocking, int pc) {
	std::string e;

    e.append("ST");
    addr = checkBoundsAndTranslate(addr);
    if (addr != nullptr) {
        if (blocking)
            e.append("B");

        if (pc != -1) {
            e.append(std::string(" @"));
            e.append(std::to_string(pc));
            e.append(std::string(" "));
        }

        e.append(toHex(addr));
        e.append(" ( ");
        for (auto dep : deps) {
            e.append(toHex(checkBoundsAndTranslate(dep)));
            e.append(" ");
        }
        e.append(" )\\n");
    }
    return e;
}

bool is_log_open() {
  log_mutex.lock();
  if (trace_bools.find(std::this_thread::get_id()) != trace_bools.end()) {
    bool is_true = trace_bools[std::this_thread::get_id()] == true;
    log_mutex.unlock();
    return is_true;
  }
  log_mutex.unlock();
  return false;
}""")

        for pe in pes:
            func = """bool check_is_""" + pe.name + "()\n"
            func += "{\n"
            func += "  " + pe.name + "_mutex.lock();\n"
            func += "  bool r = is_" + pe.name + "(get_id());\n"
            func += "  " + pe.name + "_mutex.unlock();\n"
            func += "  return r;\n"
            func += "}\n"

            print(func)

        for pe in pes:
            stall_func = """void __""" + pe.name + """_STALL(unsigned cycles, std::vector<void*> deps = {}) {
  std::string e = "STALL ";
  e.append(std::to_string(cycles));
  e.append(" ( ");

  for (auto dep : deps) {
    std::string hex = toHex(checkBoundsAndTranslate(dep));
    if (hex != "0") {
      e.append(hex);
      e.append(" ");
    }
  }

  e.append(")\\n");
  __register_entry(get_id(), e);
}"""
            print(stall_func)

        increment_stalls = '''
int increment_stalls(int num_stalls, int num_deps, void *dep1, void *dep2, void *dep3) {

  stall_mutex.lock();
  bool found_stall_counts = stall_counts.find(std::this_thread::get_id()) != stall_counts.end();
  stall_mutex.unlock();

  if (num_deps > 0) {
    if (is_log_open()) {
      emit_stall();
      emit_stall_with_deps(num_stalls, num_deps, dep1, dep2, dep3);
    }
  }
  else if (is_log_open()) {
    stall_mutex.lock();
    stall_counts[std::this_thread::get_id()] += num_stalls;
    stall_mutex.unlock();
  }
  else if (found_stall_counts) {
    stall_mutex.lock();
    stall_counts[std::this_thread::get_id()] = 0;
    stall_mutex.unlock();
  }
  return 0;
}'''
        print(increment_stalls)

        emit_stall = '''
int emit_stall() {
  std::thread::id thread_id = std::this_thread::get_id();

  stall_mutex.lock();
  bool found_stall_counts = stall_counts.find(std::this_thread::get_id()) != stall_counts.end();
  stall_mutex.unlock();

  if (is_log_open()) {
    stall_mutex.lock();
    int stalls = stall_counts[thread_id];
    stall_mutex.unlock();
    if (stalls > 0) {'''
        for pe in pes:
            emit_stall += '''
      if (check_is_'''
            emit_stall += pe.name + '()) {\n'
            emit_stall += '        __' + pe.name + '''_STALL(stalls, {});\n
        stall_mutex.lock();
        stall_counts[thread_id] = 0;
        stall_mutex.unlock();
      }'''
        emit_stall += '''
    }
  }
  else if (found_stall_counts) {
    stall_mutex.lock();
    stall_counts[std::this_thread::get_id()] = 0;
    stall_mutex.unlock();
  }
  return 0;
}\n'''

        print(emit_stall)

        func = """void __open_trace_log(unsigned tid) {\n"""
        func += """mkdir("traces/",S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);"""
        func += """  std::string fname = "./traces/pe_" + std::to_string(tid) + ".trace";
  log_mutex.lock();
  traceFiles.insert(std::make_pair(tid, std::make_shared<std::ofstream>(fname, std::ios::out)));
  if (!traceFiles.at(tid)) {
    printf("[ERROR] couldn't open trace file to write: %s\\n", fname.c_str());
    exit(1);
  }\n
"""
        func +="""  trace_bools[std::this_thread::get_id()] = true;
  log_mutex.unlock();\n"""

        func += """  stall_mutex.lock();
  stall_counts[std::this_thread::get_id()] = 0;
  stall_mutex.unlock();\n}\n"""


        print(func)

        print("""
void __close_trace_log(unsigned tid) {
  log_mutex.lock();
  if (trace_bools.find(std::this_thread::get_id()) == trace_bools.end()) {
    trace_bools[std::this_thread::get_id()] = false;
  }

  log_mutex.unlock();
  __register_entry(tid, "EOF\\n");
  log_mutex.lock();
  traceFiles.at(tid)->close();
  traceFiles.erase(tid);
  log_mutex.unlock();
}""")


        emit_load = """void emit_load(void *ptr, int pc, int num_deps, void *dep1, void *dep2, void *dep3) {
  if (is_log_open()) {
    if (shouldTranslate(ptr)) {
      std::vector<void *> deps;

      if (num_deps > 0) {
        deps.push_back(dep1);
      }

      if (num_deps > 1) {
          if (dep2 != dep1) {
              deps.push_back(dep2);
          }
      }

      if (num_deps > 2) {
          if (dep3 != dep2 && dep3 != dep1) {
              deps.push_back(dep3);
          }
      }\n"""
        for pe in pes:
            emit_load += "      if (check_is_" + pe.name + "()) {\n"
            emit_load += "        emit_stall();\n"
            #emit_load += "__" + pe.name + "_LD(ptr, deps, false, pc);\n"
            emit_load += "        __register_entry(get_id(),__LD(ptr, deps, false, pc));\n"
            emit_load += "      }\n"

        emit_load += "    }\n  }\n\n}\n"
        print(emit_load)


        emit_store = """int emit_store(void *ptr, void *value, int pc, int num_deps, void *dep1, void *dep2, void *dep3) {
  if (is_log_open()) {
    if (shouldTranslate(ptr)) {

      std::vector<void *> deps;
      if (num_deps > 0) {
        deps.push_back(dep1);
      }

      if (num_deps > 1) {
          if (dep2 != dep1) {
              deps.push_back(dep2);
          }
      }

      if (num_deps > 2) {
          if (dep3 != dep2 && dep3 != dep1) {
              deps.push_back(dep3);
          }
      }\n"""

        for pe in pes:
            emit_store += "      if (check_is_" + pe.name + "()) {\n"
            emit_store += "        emit_stall();\n"
            #emit_store += "__" + pe.name + "_ST(ptr, value, deps, false, pc);"
            emit_store += "        __register_entry(get_id(),__ST(ptr, (uint64_t)value, deps, false, pc));\n"
            emit_store += "      }\n"

        emit_store += "    }\n"
        emit_store += """
    else {
      void * p = checkBoundsAndTranslate(ptr);
      if (p) {
        fprintf(stderr, "Don't translate: %p %p\\n", ptr, p);
      }
    }
  }
  return 0;
}
"""
        print(emit_store);

        increment_stalls = """
int increment_stalls(int num_stalls, int num_deps, void *dep1 = NULL, void *dep2 = NULL, void *dep3 = NULL) {

    if (num_deps > 0) {
        if (is_log_open()) {
            emit_stall();
            emit_stall_with_deps(num_stalls, num_deps, dep1, dep2, dep3);
        }
    }
    else if (is_log_open()) {
        stall_mutex.lock();
        stall_counts[std::this_thread::get_id()] += num_stalls;
        stall_mutex.unlock();
    }
    else if (stall_counts.find(std::this_thread::get_id()) != stall_counts.end()) {
        stall_counts[std::this_thread::get_id()] = 0;
    }

    return 0;
}

"""
        emit_stall_with_deps = """
int emit_stall_with_deps(int num_stalls, int num_deps, void *dep1, void *dep2, void *dep3) {
  std::thread::id thread_id = std::this_thread::get_id();

  stall_mutex.lock();
  bool found_stall_counts = stall_counts.find(std::this_thread::get_id()) != stall_counts.end();
  stall_mutex.unlock();

  if (is_log_open()) {
    stall_mutex.lock();
    int stalls = stall_counts[thread_id];
    stall_mutex.unlock();

    std::vector<void *> deps;

      if (num_deps > 0) {
        deps.push_back(dep1);
      }

      if (num_deps > 1) {
        if (dep2 != dep1) {
          deps.push_back(dep2);
        }
      }

      if (num_deps > 2) {
        if (dep3 != dep2 && dep3 != dep1) {
          deps.push_back(dep3);
        }
      }

    if (num_stalls > 0) {
"""
        for pe in pes:
            emit_stall_with_deps += """      if (check_is_""" + pe.name + "()) {\n"
            emit_stall_with_deps += "        __" + pe.name + "_STALL(num_stalls, deps);"
            emit_stall_with_deps += """        stall_mutex.lock();
        stall_counts[thread_id] = 0;
        stall_mutex.unlock();\n      }\n"""

        emit_stall_with_deps += """
    }
  }
  else if (found_stall_counts) {
    stall_mutex.lock();
    stall_counts[std::this_thread::get_id()] = 0;
    stall_mutex.unlock();
  }
  return 0;
}"""

        print(emit_stall_with_deps)

        emit_stall = """
int emit_stall() {
  std::thread::id thread_id = std::this_thread::get_id();
  if (is_log_open()) {
    stall_mutex.lock();
    int stalls = stall_counts[thread_id];
    stall_mutex.unlock();
    if (stalls > 0) {"""

        for pe in pes:
            emit_stall += """      if (check_is_""" + pe.name + "()) {\n"
            emit_stall += "        __" + pe.name + "_STALL(num_stalls, {});"
            emit_stall += """        stall_mutex.lock();
        stall_counts[thread_id] = 0;
        stall_mutex.unlock();\n}\n"""

        emit_stall += """
      }
    }
    else if (stall_counts.find(std::this_thread::get_id()) != stall_counts.end()) {
      stall_counts[std::this_thread::get_id()] = 0;
    }
  return 0;
}"""

        for pe in pes:
            for f in pe.functions:
                func = "void " + f.trace_func + "("
                for i in range(f.num_args):
                    if (i+1) == f.num_args:
                        func += f.args[i] + " arg" + str(i)
                    else:
                        func += f.args[i] + " arg" + str(i) + ","
                func += ")"
                func += "{\n"
                func += "  if (is_log_open()) {\n"
                func += "    __register_entry(get_id(),\"" + f.token
                original_args = f.func_signature.split(',')
                for i in range(f.num_args):
                    if '*' in original_args[i]:
                        func += " \" + toHex(arg" + str(i) + ") + \" "
                    else:
                        func += " \" + std::to_string((uint64_t)arg" + str(i) + ") + \" "
                if f.num_args == 0:
                    func += ' '
                func += str(f.cycles)
                func += "\\n\");\n"

                if f.load_penalty > 0:
                    for i in range(f.load_penalty):
                        func += "    __register_entry(get_id(),\"LDB \" + toHex(arg0) + \" ( )\\n\");\n"

                if f.store_penalty > 0:
                    for i in range(f.store_penalty):
                        func += "    __register_entry(get_id(),\"STB \" + toHex(arg0) + \" ( )\\n\");\n"

                func += "  }\n}\n"
                print(func)

def generate_runtime_header(pes):
    original_stdout = sys.stdout

    with open("../tracer/runtime/default/hetsim_default_rt.h", "w") as f:
        sys.stdout = f

        print("""#ifndef COMPILER_RUNTIME_DEFAULT_HETSIM_DEFAULT_RT_H_
#define COMPILER_RUNTIME_DEFAULT_HETSIM_DEFAULT_RT_H_""")

        includes = """
#include <stdint.h>
#include <vector>
        """

        print(includes)
        print("""extern "C"
{""")

        for pe in pes:
            for f in pe.functions:
                if f.num_args == 0:
                    func_signature = f.func_signature
                else:
                    func_signature = ','.join(f.func_signature.split(',')[0:f.num_args])
                    if func_signature != f.func_signature:
                        func_signature += ')'
                print("void _C_" + pe.name + func_signature + ";")

        print("void __open_trace_log(unsigned);\n")
        print("void __close_trace_log(unsigned);\n")
        print("""void emit_load(void *ptr, int pc, int num_deps, void *dep1=NULL, void *dep2=NULL, void *dep3=NULL);
int emit_store(void *ptr, void *value, int pc, int num_deps, void *dep1=NULL, void *dep2=NULL, void *dep3=NULL);
int emit_stall_with_deps(int num_stalls, int num_deps, void *dep1=NULL, void *dep2=NULL, void *dep3=NULL);
int emit_stall();
int increment_stalls(int num_stalls, int num_deps, void *dep1, void *dep2, void *dep3);
""")
        print("""}
#endif""")

def generate_codegen():
    original_stdout = sys.stdout

    with open("../tracer/compiler-pass/hetsim-codegen.cpp", "w") as f:
        sys.stdout = f

codegen = """
//
//
//

#include "llvm/Pass.h"
// using llvm::RegisterPass

#include "llvm/IR/Type.h"
// using llvm::Type

#include "llvm/IR/DerivedTypes.h"
// using llvm::IntegerType

#include "llvm/IR/Instruction.h"
// using llvm::Instruction

#include "llvm/IR/Function.h"
// using llvm::Function

#include "llvm/IR/Module.h"
// using llvm::Module

#include "llvm/IR/LegacyPassManager.h"
// using llvm::PassManagerBase

#include "llvm/Analysis/LoopInfo.h"
// using llvm::LoopInfoWrapperPass
// using llvm::LoopInfo
// using llvm::Loop

#include "llvm/Transforms/IPO/PassManagerBuilder.h"
// using llvm::PassManagerBuilder
// using llvm::RegisterStandardPasses

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SmallSet.h"
// using llvm::SmallVector

#include "llvm/Support/CommandLine.h"
// using llvm::cl::opt
// using llvm::cl::list
// using llvm::cl::desc
// using llvm::cl::value_desc
// using llvm::cl::location
// using llvm::cl::ZeroOrMore

#include "llvm/Support/raw_ostream.h"
// using llvm::raw_ostream

#include "llvm/Support/Debug.h"
// using DEBUG macro
// using llvm::dbgs

#include <vector>
// using std::vector

#include <string>
// using std::string



#include "hetsim-analysis.hpp"

#define DEBUG_TYPE "hetsim-codegen"

// plugin registration for opt

namespace {

llvm::Loop *getTopLevelLoop(llvm::Loop *CurLoop) {
	auto *loop = CurLoop;

	while (loop && loop->getParentLoop()) {
		loop = loop->getParentLoop();
	}

	return loop;
}

class HetsimCodegenPass : public llvm::ModulePass {
public:
	static char ID;

	HetsimCodegenPass() : llvm::ModulePass(ID) {}
	bool runOnModule(llvm::Module &CurMod) override;
	void getAnalysisUsage(llvm::AnalysisUsage &AU) const override;
};

char HetsimCodegenPass::ID = 0;
static llvm::RegisterPass<HetsimCodegenPass>
Y("hetsim-codegen", "Hetsim Codegen Pass", false, true);

void HetsimCodegenPass::getAnalysisUsage(llvm::AnalysisUsage &AU) const {
	AU.addRequiredTransitive<HetsimPass>();
	return;
}

bool HetsimCodegenPass::runOnModule(llvm::Module &CurMod)
{
	return false;
}

} // namespace
"""


with open(sys.argv[1]) as json_file:
    user_spec = json.load(json_file)
    llvm_instr,addr_list,pes = parse(user_spec)
    #print(user_spec)
    generate(pes)
    generate_header(pes)
    generate_runtime(llvm_instr,addr_list,pes)
    generate_codegen()
    generate_runtime_header(pes)
    generate_util()
parsed_spec = []


