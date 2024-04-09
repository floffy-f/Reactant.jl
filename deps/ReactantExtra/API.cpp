#include "mlir-c/IR.h"
#include "mlir-c/Support.h"

#include "mlir/CAPI/IR.h"

#include "Enzyme/MLIR/Dialect/Dialect.h"
#include "Enzyme/MLIR/Dialect/Ops.h"
#include "Enzyme/MLIR/Implementations/CoreDialectsAutoDiffImplementations.h"
#include "Enzyme/MLIR/Passes/Passes.h"
#include "src/enzyme_ad/jax/Implementations/XLADerivatives.h"
#include "src/enzyme_ad/jax/Passes/Passes.h"
#include "src/enzyme_ad/jax/TransformOps/TransformOps.h"
#include "mlir/Conversion/Passes.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Async/IR/Async.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/DLTI/DLTI.h"
#include "mlir/Dialect/Func/Extensions/InlinerExtension.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/NVVMDialect.h"
#include "mlir/Dialect/Linalg/IR/Linalg.h"
#include "mlir/Dialect/Linalg/TransformOps/DialectExtension.h"
#include "mlir/Dialect/Math/IR/Math.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/OpenMP/OpenMPDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/Transform/Transforms/Passes.h"
#include "mlir/InitAllPasses.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/Passes.h"

#include "stablehlo/dialect/ChloOps.h"
#include "stablehlo/dialect/StablehloOps.h"

#include "xla/mlir_hlo/mhlo/IR/hlo_ops.h"
#include "xla/mlir/utils/type_util.h"
#include "xla/pjrt/gpu/se_gpu_pjrt_client.h"
#include "xla/pjrt/cpu/cpu_client.h"
#include "xla/pjrt/status_casters.h"
#include "xla/pjrt/pjrt_executable.h"
#include "xla/python/ifrt/executable.h"

#include "xla/python/pjrt_ifrt/xla_compiler.h"

using namespace mlir;
using namespace llvm;
using namespace xla;

// int google::protobuf::io::CodedInputStream::default_recursion_limit_ = 100;
// int xla::_LayoutProto_default_instance_;

extern "C" PjRtClient* MakeCPUClient(uint8_t asynchronous, int node_id, int num_nodes) {
    CpuClientOptions options;
    // options.kv_store = "etcd";
    options.node_id = node_id;
    options.num_nodes = num_nodes;
    // options.collectives = num_nodes;
    options.asynchronous = asynchronous != 0;
    auto client = xla::ValueOrThrow(GetTfrtCpuClient(options));
    return client.release();
}

// xla/python/xla.cc 390
extern "C" PjRtClient* MakeGPUClient(int node_id, int num_nodes, int* allowed_devices, int num_allowed_devices, const char* platform_name) {
    GpuClientOptions options;
    // options.kv_store = "etcd";
    // options.allocator_config = 
    options.node_id = node_id;
    options.num_nodes = num_nodes;
    options.allowed_devices = std::set<int>(allowed_devices, allowed_devices + num_allowed_devices);
    options.platform_name = std::string(platform_name);
    // options.collectives = num_nodes;
    auto client = xla::ValueOrThrow(GetStreamExecutorGpuClient(options));
    return client.release();
}

extern "C" int ClientNumDevices(PjRtClient* client) {
    return client->device_count();
}

extern "C" int ClientNumAddressableDevices(PjRtClient* client) {
    return client->addressable_device_count();
}

extern "C" int ClientProcessIndex(PjRtClient* client) {
    return client->process_index();
}

extern "C" PjRtDevice* ClientGetDevice(PjRtClient* client, int device_id) {
    return xla::ValueOrThrow(client->LookupDevice(device_id));
}

extern "C" PjRtDevice* ClientGetAddressableDevice(PjRtClient* client, int device_id) {
    return xla::ValueOrThrow(client->LookupAddressableDevice(device_id));
}

extern "C" void ExecutableFree(xla::PjRtLoadedExecutable* exec) {
    delete exec;
}

extern "C" PjRtDevice* BufferToDevice(PjRtBuffer* Buffer) {
    return Buffer->device();
}

extern "C" PjRtClient* BufferToClient(PjRtBuffer* Buffer) {
    return Buffer->client();
}

extern "C" PjRtClient* DeviceToClient(PjRtDevice* Device) {
    return Device->client();
}

extern "C" void PjRtBufferFree(PjRtBuffer* Buffer) {
    delete Buffer;
}

static void noop() {}

extern "C" PjRtBuffer* ArrayFromHostBuffer(PjRtClient* client, void* data, MlirType mtype, size_t dim, int64_t* cshape, PjRtDevice* device) {
    auto primtype = ConvertMlirTypeToPrimitiveType(unwrap(mtype));
    absl::Span<const int64_t> shape(cshape, dim);
    PjRtClient::HostBufferSemantics semantics = PjRtClient::HostBufferSemantics::kImmutableOnlyDuringCall;
    auto buffer = xla::ValueOrThrow(client->BufferFromHostBuffer(data, primtype, shape, /*byte_strides*/{},  semantics, /*ondone*/{}, device));
    auto bres = buffer.release();
    return bres;
}

extern "C" uint8_t BufferOnCPU(PjRtBuffer* buffer) {
    return buffer->IsOnCpu();
}

extern "C" void* UnsafeBufferPointer(PjRtBuffer* buffer) {
    auto unsafe = xla::ValueOrThrow(buffer->client()->UnsafeBufferPointer(buffer));
    return (void*)unsafe;
}

extern "C" PjRtBuffer* CopyBufferToDevice(PjRtBuffer* buffer, PjRtDevice* dst_device) {
    auto res = xla::ValueOrThrow(buffer->CopyToDevice(dst_device));
    return res.release();
}

extern "C" void BufferToHost(PjRtBuffer* buffer, void* data) {
    MutableBorrowingLiteral literal((const char*)data, xla::ValueOrThrow(buffer->HostShape()));
    auto status = buffer->ToLiteralSync(&literal);
    if (!status.ok()) {
        printf("error copying to host: %s\n", status.ToString().c_str());
    }
}

extern "C" void FreeClient(PjRtClient * client) {
    delete client;
}

/* Note that this */
extern "C" xla::PjRtLoadedExecutable* ClientCompile(PjRtClient * client, MlirModule cmod) {
    auto program = std::make_unique<xla::ifrt::XlaProgram>(cast<ModuleOp>(*unwrap(cmod)));

    CompileOptions options;
    // options.argument_layouts;
    // options.executable_build_options.set_device_ordinal();
    // options.executable_build_options.set_result_layout();
    
    auto addressable_devices = client->addressable_devices();
    if (!addressable_devices.empty()) {
        int device_ordinal = options.executable_build_options.device_ordinal();
        if (device_ordinal < 0) {
            device_ordinal = 0;
        }
        assert(device_ordinal < addressable_devices.size());
        auto stats = addressable_devices[device_ordinal]->GetAllocatorStats();
        if (stats.ok() && stats->bytes_limit) {
            options.executable_build_options.set_device_memory_size(*stats->bytes_limit);
        }
    }
    auto exec = xla::ValueOrThrow(client->Compile(cast<ModuleOp>(*unwrap(cmod)), options));
    return exec.release();
}

extern "C" void FreeFuture(PjRtFuture<Status>* Future) {
    delete Future;
}

extern "C" uint8_t FutureIsReady(PjRtFuture<Status>* Future) {
    return Future->IsReady();
}

extern "C" void FutureAwait(PjRtFuture<Status>* Future) {
    Future->Await();
}

extern "C" void XLAExecute(xla::PjRtLoadedExecutable* exec, int num_args, PjRtBuffer** op_args, uint8_t* is_arg_donatable, int num_results, PjRtBuffer** op_results, uint8_t *futures, PjRtFuture<Status>** future_results) {
    std::vector<std::vector<PjRtBuffer*>> argument_handles;
    for (size_t i=0; i<num_args; i++) {
        argument_handles.emplace_back(1, op_args[i]);
    }
    ExecuteOptions options;

    for (size_t i=0; i<num_args; i++) {
        if (!is_arg_donatable[i])
            options.non_donatable_input_indices.insert((int)i);
    }
    std::optional<std::vector<PjRtFuture<Status>>> returned_futures;
    auto results = xla::ValueOrThrow(exec->Execute(static_cast<absl::Span<const std::vector<PjRtBuffer*>>>(argument_handles), options, returned_futures));

    assert(results.size() == num_results);
    if (returned_futures) {
        *futures = true;
        assert(returned_futures->size() == num_results);
        for (size_t i=0; i<num_results; i++) {
            future_results[i] = new PjRtFuture<Status>((*returned_futures)[i]);
        }
    } else {
        *futures = false;
    }

    for (size_t i=0; i<num_results; i++) {
        assert(results[i].size() == 1);
        op_results[i] = results[i][0].release();
    }
}

extern "C" void RegisterDialects(MlirContext cctx) {
  mlir::MLIRContext &context = *unwrap(cctx);
  context.loadDialect<mlir::arith::ArithDialect>();
  context.loadDialect<mlir::enzyme::EnzymeDialect>();
  context.loadDialect<mlir::tensor::TensorDialect>();
  context.loadDialect<mlir::func::FuncDialect>();
  context.loadDialect<mlir::mhlo::MhloDialect>();
  context.loadDialect<mlir::stablehlo::StablehloDialect>();
  context.loadDialect<mlir::chlo::ChloDialect>();
}
extern "C" void InitializeRegistryAndPasses(MlirDialectRegistry creg) {
  mlir::DialectRegistry &registry = *unwrap(creg);

  // Register MLIR stuff
  registry.insert<mlir::affine::AffineDialect>();
  registry.insert<mlir::LLVM::LLVMDialect>();
  registry.insert<mlir::memref::MemRefDialect>();
  registry.insert<mlir::async::AsyncDialect>();
  registry.insert<mlir::tensor::TensorDialect>();
  registry.insert<mlir::func::FuncDialect>();
  registry.insert<mlir::arith::ArithDialect>();
  registry.insert<mlir::cf::ControlFlowDialect>();
  registry.insert<mlir::scf::SCFDialect>();
  registry.insert<mlir::gpu::GPUDialect>();
  registry.insert<mlir::NVVM::NVVMDialect>();
  registry.insert<mlir::omp::OpenMPDialect>();
  registry.insert<mlir::math::MathDialect>();
  registry.insert<mlir::linalg::LinalgDialect>();
  registry.insert<DLTIDialect>();
  registry.insert<mlir::mhlo::MhloDialect>();
  registry.insert<mlir::stablehlo::StablehloDialect>();
  registry.insert<mlir::chlo::ChloDialect>();

  registry.insert<mlir::enzyme::EnzymeDialect>();

  mlir::registerenzymePasses();
  regsiterenzymeXLAPasses();
  mlir::enzyme::registerXLAAutoDiffInterfaces(registry);

  mlir::func::registerInlinerExtension(registry);

  // Register the standard passes we want.
  mlir::registerCSEPass();
  mlir::registerConvertAffineToStandardPass();
  mlir::registerSCCPPass();
  mlir::registerInlinerPass();
  mlir::registerCanonicalizerPass();
  mlir::registerSymbolDCEPass();
  mlir::registerLoopInvariantCodeMotionPass();
  mlir::registerConvertSCFToOpenMPPass();
  mlir::affine::registerAffinePasses();
  mlir::registerReconcileUnrealizedCasts();

/*
  registry.addExtension(+[](MLIRContext *ctx, LLVM::LLVMDialect *dialect) {
    LLVM::LLVMFunctionType::attachInterface<MemRefInsider>(*ctx);
    LLVM::LLVMArrayType::attachInterface<MemRefInsider>(*ctx);
    LLVM::LLVMPointerType::attachInterface<MemRefInsider>(*ctx);
    LLVM::LLVMStructType::attachInterface<MemRefInsider>(*ctx);
    MemRefType::attachInterface<PtrElementModel<MemRefType>>(*ctx);
    LLVM::LLVMStructType::attachInterface<
        PtrElementModel<LLVM::LLVMStructType>>(*ctx);
    LLVM::LLVMPointerType::attachInterface<
        PtrElementModel<LLVM::LLVMPointerType>>(*ctx);
    LLVM::LLVMArrayType::attachInterface<PtrElementModel<LLVM::LLVMArrayType>>(
        *ctx);
  });
  */

  // Register the autodiff interface implementations for upstream dialects.
  enzyme::registerCoreDialectAutodiffInterfaces(registry);

  // Transform dialect and extensions.
  mlir::transform::registerInterpreterPass();
  mlir::linalg::registerTransformDialectExtension(registry);
  mlir::enzyme::registerGenerateApplyPatternsPass();
  mlir::enzyme::registerRemoveTransformPass();
  mlir::enzyme::registerEnzymeJaxTransformExtension(registry);
}