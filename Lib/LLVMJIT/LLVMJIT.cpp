#include <utility>

#include "Inline/Assert.h"
#include "Inline/BasicTypes.h"
#include "Inline/Errors.h"
#include "LLVMJIT/LLVMJIT.h"
#include "LLVMJITPrivate.h"

#include "LLVMPreInclude.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/TargetSelect.h"

#include "LLVMPostInclude.h"

namespace llvm
{
	class Constant;
}

using namespace IR;
using namespace LLVMJIT;

static std::map<std::string, const char*> runtimeSymbolMap = {
#ifdef _WIN32
	// the LLVM X86 code generator calls __chkstk when allocating more than 4KB of stack space
	{"__chkstk", "__chkstk"},
	{"__C_specific_handler", "__C_specific_handler"},
#ifndef _WIN64
	{"__aullrem", "_aullrem"},
	{"__allrem", "_allrem"},
	{"__aulldiv", "_aulldiv"},
	{"__alldiv", "_alldiv"},
#endif
#else
	{"__CxxFrameHandler3", "__CxxFrameHandler3"},
	{"__cxa_begin_catch", "__cxa_begin_catch"},
	{"__gxx_personality_v0", "__gxx_personality_v0"},
#endif
#ifdef __arm__
	{"__aeabi_uidiv", "__aeabi_uidiv"},
	{"__aeabi_idiv", "__aeabi_idiv"},
	{"__aeabi_idivmod", "__aeabi_idivmod"},
	{"__aeabi_uldiv", "__aeabi_uldiv"},
	{"__aeabi_uldivmod", "__aeabi_uldivmod"},
	{"__aeabi_unwind_cpp_pr0", "__aeabi_unwind_cpp_pr0"},
	{"__aeabi_unwind_cpp_pr1", "__aeabi_unwind_cpp_pr1"},
#endif
};

llvm::JITEvaluatedSymbol LLVMJIT::resolveJITImport(llvm::StringRef name)
{
	// Allow some intrinsics used by LLVM
	auto runtimeSymbolNameIt = runtimeSymbolMap.find(name);
	if(runtimeSymbolNameIt == runtimeSymbolMap.end()) { return llvm::JITEvaluatedSymbol(nullptr); }

	const char* lookupName = runtimeSymbolNameIt->second;
	void* addr = llvm::sys::DynamicLibrary::SearchForAddressOfSymbol(lookupName);
	if(!addr)
	{
		Errors::fatalf("LLVM generated code references undefined external symbol: %s\n",
					   lookupName);
	}
	return llvm::JITEvaluatedSymbol(reinterpret_cast<Uptr>(addr), llvm::JITSymbolFlags::None);
}

static bool globalInitLLVM()
{
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	llvm::InitializeNativeTargetAsmParser();
	llvm::InitializeNativeTargetDisassembler();
	llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
	return true;
}

LLVMContext::LLVMContext()
{
	static bool isLLVMInitialized = globalInitLLVM();
	wavmAssert(isLLVMInitialized);

	i8Type = llvm::Type::getInt8Ty(*this);
	i16Type = llvm::Type::getInt16Ty(*this);
	i32Type = llvm::Type::getInt32Ty(*this);
	i64Type = llvm::Type::getInt64Ty(*this);
	i128Type = llvm::Type::getInt128Ty(*this);
	f32Type = llvm::Type::getFloatTy(*this);
	f64Type = llvm::Type::getDoubleTy(*this);
	i8PtrType = i8Type->getPointerTo();
	switch(sizeof(Uptr))
	{
	case 4: iptrType = i32Type; break;
	case 8: iptrType = i64Type; break;
	default: Errors::unreachable();
	}

	auto llvmExceptionRecordStructType = llvm::StructType::create({
		i32Type,   // DWORD ExceptionCode
		i32Type,   // DWORD ExceptionFlags
		i8PtrType, // _EXCEPTION_RECORD* ExceptionRecord
		i8PtrType, // PVOID ExceptionAddress
		i32Type,   // DWORD NumParameters
		llvm::ArrayType::get(i64Type,
							 15) // ULONG_PTR ExceptionInformation[EXCEPTION_MAXIMUM_PARAMETERS]
	});
	exceptionPointersStructType
		= llvm::StructType::create({llvmExceptionRecordStructType->getPointerTo(), i8PtrType});

	i8x16Type = llvm::VectorType::get(i8Type, 16);
	i16x8Type = llvm::VectorType::get(i16Type, 8);
	i32x4Type = llvm::VectorType::get(i32Type, 4);
	i64x2Type = llvm::VectorType::get(i64Type, 2);
	i128x1Type = llvm::VectorType::get(i128Type, 1);
	f32x4Type = llvm::VectorType::get(f32Type, 4);
	f64x2Type = llvm::VectorType::get(f64Type, 2);

	valueTypes[(Uptr)ValueType::i32] = i32Type;
	valueTypes[(Uptr)ValueType::i64] = i64Type;
	valueTypes[(Uptr)ValueType::f32] = f32Type;
	valueTypes[(Uptr)ValueType::f64] = f64Type;
	valueTypes[(Uptr)ValueType::v128] = i128x1Type;

	// Create zero constants of each type.
	typedZeroConstants[(Uptr)ValueType::any] = nullptr;
	typedZeroConstants[(Uptr)ValueType::i32] = emitLiteral(*this, (U32)0);
	typedZeroConstants[(Uptr)ValueType::i64] = emitLiteral(*this, (U64)0);
	typedZeroConstants[(Uptr)ValueType::f32] = emitLiteral(*this, (F32)0.0f);
	typedZeroConstants[(Uptr)ValueType::f64] = emitLiteral(*this, (F64)0.0);

	U64 i64x2Zero[2] = {0, 0};
	typedZeroConstants[(Uptr)ValueType::v128] = llvm::ConstantVector::get(
		{llvm::ConstantInt::get(i128Type, llvm::APInt(128, 2, i64x2Zero))});
}