// Copyright 2016 the V8 project authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_OBJECTS_H_
#define V8_WASM_WASM_OBJECTS_H_

#include <memory>

#include "src/base/bit-field.h"
#include "src/debug/interface-types.h"
#include "src/heap/heap.h"
#include "src/objects/backing-store.h"
#include "src/objects/foreign.h"
#include "src/objects/js-function.h"
#include "src/objects/js-objects.h"
#include "src/objects/objects-body-descriptors.h"
#include "src/objects/objects.h"
#include "src/objects/struct.h"
#include "src/objects/trusted-object.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/stacks.h"
#include "src/wasm/struct-types.h"
#include "src/wasm/value-type.h"

// Has to be the last include (doesn't have include guards)
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {
namespace wasm {
class NativeModule;
class WasmCode;
struct WasmFunction;
struct WasmGlobal;
struct WasmModule;
struct WasmTag;
using WasmTagSig = FunctionSig;
class WasmValue;
class WireBytesRef;
}  // namespace wasm

class BreakPoint;
class JSArrayBuffer;
class SeqOneByteString;
class StructBodyDescriptor;
class WasmCapiFunction;
class WasmExceptionTag;
class WasmExportedFunction;
class WasmExternalFunction;
class WasmTrustedInstanceData;
class WasmJSFunction;
class WasmModuleObject;

enum class SharedFlag : uint8_t;

template <class CppType>
class Managed;

#include "torque-generated/src/wasm/wasm-objects-tq.inc"

#define DECL_OPTIONAL_ACCESSORS(name, type) \
  DECL_GETTER(has_##name, bool)             \
  DECL_ACCESSORS(name, type)

class V8_EXPORT_PRIVATE FunctionTargetAndRef {
 public:
  FunctionTargetAndRef(Handle<WasmInstanceObject> target_instance_object,
                       int target_func_index);
  Handle<Object> ref() { return ref_; }
  Address call_target() { return call_target_; }

 private:
  Handle<Object> ref_;
  Address call_target_;
};

namespace wasm {
enum Suspend : bool { kSuspend = true, kNoSuspend = false };
enum Promise : bool { kPromise = true, kNoPromise = false };
enum class OnResume : int { kContinue, kThrow };
}  // namespace wasm

// A helper for an entry for an imported function, indexed statically.
// The underlying storage in the instance is used by generated code to
// call imported functions at runtime.
// Each entry is either:
//   - Wasm to JS, which has fields
//      - object = a WasmApiFunctionRef
//      - target = entrypoint to import wrapper code
//   - Wasm to Wasm, which has fields
//      - object = target instance
//      - target = entrypoint for the function
class ImportedFunctionEntry {
 public:
  inline ImportedFunctionEntry(Handle<WasmInstanceObject>, int index);
  inline ImportedFunctionEntry(Isolate*, Handle<WasmTrustedInstanceData>,
                               int index);

  // Initialize this entry as a Wasm to JS call. This accepts the isolate as a
  // parameter, since it must allocate a tuple.
  void SetWasmToJs(Isolate*, Handle<JSReceiver> callable, wasm::Suspend suspend,
                   const wasm::FunctionSig* sig);
  V8_EXPORT_PRIVATE void SetWasmToJs(Isolate*, Handle<JSReceiver> callable,
                                     const wasm::WasmCode* wasm_to_js_wrapper,
                                     wasm::Suspend suspend,
                                     const wasm::FunctionSig* sig);

  // Initialize this entry as a Wasm to Wasm call.
  void SetWasmToWasm(Tagged<WasmInstanceObject> target_instance_object,
                     Address call_target);

  Tagged<JSReceiver> callable();
  Tagged<Object> maybe_callable();
  Tagged<Object> object_ref();
  Address target();
  void set_target(Address new_target);

 private:
  // TODO(14499): Less back-and-forth between trusted and untrusted data.
  Handle<WasmInstanceObject> const instance_object_;
  int const index_;
};

enum InternalizeString : bool { kInternalize = true, kNoInternalize = false };

// Representation of a WebAssembly.Module JavaScript-level object.
class WasmModuleObject
    : public TorqueGeneratedWasmModuleObject<WasmModuleObject, JSObject> {
 public:
  inline wasm::NativeModule* native_module() const;
  inline const std::shared_ptr<wasm::NativeModule>& shared_native_module()
      const;
  inline const wasm::WasmModule* module() const;

  // Dispatched behavior.
  DECL_PRINTER(WasmModuleObject)

  // Creates a new {WasmModuleObject} for an existing {NativeModule} that is
  // reference counted and might be shared between multiple Isolates.
  V8_EXPORT_PRIVATE static Handle<WasmModuleObject> New(
      Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
      Handle<Script> script);

  // Check whether this module was generated from asm.js source.
  inline bool is_asm_js();

  // Get the module name, if set. Returns an empty handle otherwise.
  static MaybeHandle<String> GetModuleNameOrNull(Isolate*,
                                                 Handle<WasmModuleObject>);

  // Get the function name of the function identified by the given index.
  // Returns a null handle if the function is unnamed or the name is not a valid
  // UTF-8 string.
  static MaybeHandle<String> GetFunctionNameOrNull(Isolate*,
                                                   Handle<WasmModuleObject>,
                                                   uint32_t func_index);

  // Get the raw bytes of the function name of the function identified by the
  // given index.
  // Meant to be used for debugging or frame printing.
  // Does not allocate, hence gc-safe.
  base::Vector<const uint8_t> GetRawFunctionName(int func_index);

  // Extract a portion of the wire bytes as UTF-8 string, optionally
  // internalized. (Prefer to internalize early if the string will be used for a
  // property lookup anyway.)
  static Handle<String> ExtractUtf8StringFromModuleBytes(
      Isolate*, Handle<WasmModuleObject>, wasm::WireBytesRef,
      InternalizeString);
  static Handle<String> ExtractUtf8StringFromModuleBytes(
      Isolate*, base::Vector<const uint8_t> wire_byte, wasm::WireBytesRef,
      InternalizeString);

  TQ_OBJECT_CONSTRUCTORS(WasmModuleObject)
};

// Representation of a WebAssembly.Table JavaScript-level object.
class WasmTableObject
    : public TorqueGeneratedWasmTableObject<WasmTableObject, JSObject> {
 public:
  inline wasm::ValueType type();

  V8_EXPORT_PRIVATE static int Grow(Isolate* isolate,
                                    Handle<WasmTableObject> table,
                                    uint32_t count, Handle<Object> init_value);

  V8_EXPORT_PRIVATE static Handle<WasmTableObject> New(
      Isolate* isolate, Handle<WasmInstanceObject> instance_object,
      wasm::ValueType type, uint32_t initial, bool has_maximum,
      uint32_t maximum, Handle<Object> initial_value);

  V8_EXPORT_PRIVATE static void AddDispatchTable(
      Isolate* isolate, Handle<WasmTableObject> table,
      Handle<WasmTrustedInstanceData> instance_object, int table_index);

  bool is_in_bounds(uint32_t entry_index);

  // Thin wrapper around {JsToWasmObject}.
  static MaybeHandle<Object> JSToWasmElement(Isolate* isolate,
                                             Handle<WasmTableObject> table,
                                             Handle<Object> entry,
                                             const char** error_message);

  // This function will not handle JS objects; i.e., {entry} needs to be in wasm
  // representation.
  V8_EXPORT_PRIVATE static void Set(Isolate* isolate,
                                    Handle<WasmTableObject> table,
                                    uint32_t index, Handle<Object> entry);

  V8_EXPORT_PRIVATE static Handle<Object> Get(Isolate* isolate,
                                              Handle<WasmTableObject> table,
                                              uint32_t index);

  V8_EXPORT_PRIVATE static void Fill(Isolate* isolate,
                                     Handle<WasmTableObject> table,
                                     uint32_t start, Handle<Object> entry,
                                     uint32_t count);

  // TODO(wasm): Unify these three methods into one.
  static void UpdateDispatchTables(
      Isolate* isolate, Handle<WasmTableObject> table, int entry_index,
      const wasm::WasmFunction* func,
      Handle<WasmTrustedInstanceData> target_instance);
  static void UpdateDispatchTables(Isolate* isolate,
                                   Handle<WasmTableObject> table,
                                   int entry_index,
                                   Handle<WasmJSFunction> function);
  static void UpdateDispatchTables(Isolate* isolate,
                                   Handle<WasmTableObject> table,
                                   int entry_index,
                                   Handle<WasmCapiFunction> capi_function);

  void ClearDispatchTables(int index);

  V8_EXPORT_PRIVATE static void SetFunctionTablePlaceholder(
      Isolate* isolate, Handle<WasmTableObject> table, int entry_index,
      Handle<WasmTrustedInstanceData> trusted_instance_data, int func_index);

  // This function reads the content of a function table entry and returns it
  // through the output parameters.
  static void GetFunctionTableEntry(
      Isolate* isolate, const wasm::WasmModule* module,
      Handle<WasmTableObject> table, int entry_index, bool* is_valid,
      bool* is_null, MaybeHandle<WasmInstanceObject>* instance_object,
      int* function_index, MaybeHandle<WasmJSFunction>* maybe_js_function);

 private:
  // {entry} is either {Null} or a {WasmInternalFunction}.
  static void SetFunctionTableEntry(Isolate* isolate,
                                    Handle<WasmTableObject> table,
                                    Handle<FixedArray> entries, int entry_index,
                                    Handle<Object> entry);

  TQ_OBJECT_CONSTRUCTORS(WasmTableObject)
};

// Representation of a WebAssembly.Memory JavaScript-level object.
class WasmMemoryObject
    : public TorqueGeneratedWasmMemoryObject<WasmMemoryObject, JSObject> {
 public:
  DECL_ACCESSORS(instances, Tagged<WeakArrayList>)

  // Add a use of this memory object to the given instance. This updates the
  // internal weak list of instances that use this memory and also updates the
  // fields of the instance to reference this memory's buffer.
  V8_EXPORT_PRIVATE static void UseInInstance(
      Isolate* isolate, Handle<WasmMemoryObject> memory,
      Handle<WasmTrustedInstanceData> trusted_instance_data,
      int memory_index_in_instance);
  inline bool has_maximum_pages();

  // Overwrite the Torque-generated method that returns an int.
  inline bool is_memory64() const;

  V8_EXPORT_PRIVATE static Handle<WasmMemoryObject> New(
      Isolate* isolate, Handle<JSArrayBuffer> buffer, int maximum,
      WasmMemoryFlag memory_type);

  V8_EXPORT_PRIVATE static MaybeHandle<WasmMemoryObject> New(
      Isolate* isolate, int initial, int maximum, SharedFlag shared,
      WasmMemoryFlag memory_type);

  // Assign a new (grown) buffer to this memory, also updating the shortcut
  // fields of all instances that use this memory.
  void SetNewBuffer(Tagged<JSArrayBuffer> new_buffer);

  V8_EXPORT_PRIVATE static int32_t Grow(Isolate*, Handle<WasmMemoryObject>,
                                        uint32_t pages);

  static constexpr int kNoMaximum = -1;

  TQ_OBJECT_CONSTRUCTORS(WasmMemoryObject)
};

// Representation of a WebAssembly.Global JavaScript-level object.
class WasmGlobalObject
    : public TorqueGeneratedWasmGlobalObject<WasmGlobalObject, JSObject> {
 public:
  DECL_ACCESSORS(untagged_buffer, Tagged<JSArrayBuffer>)
  DECL_ACCESSORS(tagged_buffer, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(type, wasm::ValueType)

  // Dispatched behavior.
  DECL_PRINTER(WasmGlobalObject)

  V8_EXPORT_PRIVATE static MaybeHandle<WasmGlobalObject> New(
      Isolate* isolate, Handle<WasmInstanceObject> instance_object,
      MaybeHandle<JSArrayBuffer> maybe_untagged_buffer,
      MaybeHandle<FixedArray> maybe_tagged_buffer, wasm::ValueType type,
      int32_t offset, bool is_mutable);

  inline int type_size() const;

  inline int32_t GetI32();
  inline int64_t GetI64();
  inline float GetF32();
  inline double GetF64();
  inline uint8_t* GetS128RawBytes();
  inline Handle<Object> GetRef();

  inline void SetI32(int32_t value);
  inline void SetI64(int64_t value);
  inline void SetF32(float value);
  inline void SetF64(double value);
  // {value} must be an object in Wasm representation.
  inline void SetRef(Handle<Object> value);

 private:
  // This function returns the address of the global's data in the
  // JSArrayBuffer. This buffer may be allocated on-heap, in which case it may
  // not have a fixed address.
  inline Address address() const;

  TQ_OBJECT_CONSTRUCTORS(WasmGlobalObject)
};

// The trusted part of a WebAssembly instance.
// This object lives in trusted space and is never modified from user space.
class V8_EXPORT_PRIVATE WasmTrustedInstanceData : public ExposedTrustedObject {
 public:
  DECL_CAST(WasmTrustedInstanceData)

  DECL_ACCESSORS(instance_object, Tagged<WasmInstanceObject>)
  DECL_ACCESSORS(native_context, Tagged<Context>)
  DECL_ACCESSORS(memory_objects, Tagged<FixedArray>)
  DECL_OPTIONAL_ACCESSORS(untagged_globals_buffer, Tagged<JSArrayBuffer>)
  DECL_OPTIONAL_ACCESSORS(tagged_globals_buffer, Tagged<FixedArray>)
  DECL_OPTIONAL_ACCESSORS(imported_mutable_globals_buffers, Tagged<FixedArray>)
  DECL_OPTIONAL_ACCESSORS(tables, Tagged<FixedArray>)
  DECL_OPTIONAL_ACCESSORS(indirect_function_tables, Tagged<FixedArray>)
  DECL_ACCESSORS(imported_function_refs, Tagged<FixedArray>)
  DECL_ACCESSORS(imported_mutable_globals, Tagged<FixedAddressArray>)
  DECL_ACCESSORS(imported_function_targets, Tagged<FixedAddressArray>)
  DECL_OPTIONAL_ACCESSORS(indirect_function_table_refs, Tagged<FixedArray>)
  DECL_ACCESSORS(indirect_function_table_sig_ids, Tagged<FixedUInt32Array>)
  DECL_ACCESSORS(indirect_function_table_targets, Tagged<ExternalPointerArray>)
  DECL_OPTIONAL_ACCESSORS(tags_table, Tagged<FixedArray>)
  DECL_ACCESSORS(wasm_internal_functions, Tagged<FixedArray>)
  DECL_ACCESSORS(managed_object_maps, Tagged<FixedArray>)
  DECL_ACCESSORS(feedback_vectors, Tagged<FixedArray>)
  DECL_ACCESSORS(well_known_imports, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(memory0_start, uint8_t*)
  DECL_PRIMITIVE_ACCESSORS(memory0_size, size_t)
  DECL_PRIMITIVE_ACCESSORS(new_allocation_limit_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(new_allocation_top_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(old_allocation_limit_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(old_allocation_top_address, Address*)
  DECL_PRIMITIVE_ACCESSORS(isorecursive_canonical_types, const uint32_t*)
  DECL_PRIMITIVE_ACCESSORS(globals_start, uint8_t*)
  DECL_PRIMITIVE_ACCESSORS(indirect_function_table_size, uint32_t)
  DECL_PRIMITIVE_ACCESSORS(jump_table_start, Address)
  DECL_PRIMITIVE_ACCESSORS(hook_on_function_call_address, Address)
  DECL_PRIMITIVE_ACCESSORS(tiering_budget_array, uint32_t*)
  DECL_ACCESSORS(memory_bases_and_sizes, Tagged<FixedAddressArray>)
  DECL_ACCESSORS(data_segment_starts, Tagged<FixedAddressArray>)
  DECL_ACCESSORS(data_segment_sizes, Tagged<FixedUInt32Array>)
  DECL_ACCESSORS(element_segments, Tagged<FixedArray>)
  DECL_PRIMITIVE_ACCESSORS(break_on_entry, uint8_t)

  // Clear uninitialized padding space. This ensures that the snapshot content
  // is deterministic. Depending on the V8 build mode there could be no padding.
  V8_INLINE void clear_padding();

  inline Tagged<WasmMemoryObject> memory_object(int memory_index) const;
  inline uint8_t* memory_base(int memory_index) const;
  inline size_t memory_size(int memory_index) const;

  inline Tagged<WasmModuleObject> module_object() const;
  inline const wasm::WasmModule* module() const;

  // Dispatched behavior.
  DECL_PRINTER(WasmTrustedInstanceData)
  DECL_VERIFIER(WasmTrustedInstanceData)

// Layout description.
#define FIELD_LIST(V)                                                     \
  /* Often-accessed fields go first to minimize generated code size. */   \
  /* Less than system pointer sized fields come first. */                 \
  V(kImportedFunctionRefsOffset, kTaggedSize)                             \
  V(kIndirectFunctionTableRefsOffset, kTaggedSize)                        \
  V(kIndirectFunctionTableSigIdsOffset, kTaggedSize)                      \
  V(kIndirectFunctionTableTargetsOffset, kTaggedSize)                     \
  V(kImportedMutableGlobalsOffset, kTaggedSize)                           \
  V(kImportedFunctionTargetsOffset, kTaggedSize)                          \
  V(kIndirectFunctionTableSizeOffset, kUInt32Size)                        \
  /* Optional padding to align system pointer size fields */              \
  V(kOptionalPaddingOffset, POINTER_SIZE_PADDING(kOptionalPaddingOffset)) \
  V(kMemory0StartOffset, kSystemPointerSize)                              \
  V(kMemory0SizeOffset, kSizetSize)                                       \
  V(kIsorecursiveCanonicalTypesOffset, kSystemPointerSize)                \
  V(kGlobalsStartOffset, kSystemPointerSize)                              \
  V(kJumpTableStartOffset, kSystemPointerSize)                            \
  /* End of often-accessed fields. */                                     \
  /* Continue with system pointer size fields to maintain alignment. */   \
  V(kNewAllocationLimitAddressOffset, kSystemPointerSize)                 \
  V(kNewAllocationTopAddressOffset, kSystemPointerSize)                   \
  V(kOldAllocationLimitAddressOffset, kSystemPointerSize)                 \
  V(kOldAllocationTopAddressOffset, kSystemPointerSize)                   \
  V(kHookOnFunctionCallAddressOffset, kSystemPointerSize)                 \
  V(kTieringBudgetArrayOffset, kSystemPointerSize)                        \
  /* Less than system pointer size aligned fields are below. */           \
  V(kMemoryBasesAndSizesOffset, kTaggedSize)                              \
  V(kDataSegmentStartsOffset, kTaggedSize)                                \
  V(kDataSegmentSizesOffset, kTaggedSize)                                 \
  V(kElementSegmentsOffset, kTaggedSize)                                  \
  V(kInstanceObjectOffset, kTaggedSize)                                   \
  V(kNativeContextOffset, kTaggedSize)                                    \
  V(kMemoryObjectsOffset, kTaggedSize)                                    \
  V(kUntaggedGlobalsBufferOffset, kTaggedSize)                            \
  V(kTaggedGlobalsBufferOffset, kTaggedSize)                              \
  V(kImportedMutableGlobalsBuffersOffset, kTaggedSize)                    \
  V(kTablesOffset, kTaggedSize)                                           \
  V(kIndirectFunctionTablesOffset, kTaggedSize)                           \
  V(kTagsTableOffset, kTaggedSize)                                        \
  V(kWasmInternalFunctionsOffset, kTaggedSize)                            \
  V(kManagedObjectMapsOffset, kTaggedSize)                                \
  V(kFeedbackVectorsOffset, kTaggedSize)                                  \
  V(kWellKnownImportsOffset, kTaggedSize)                                 \
  V(kBreakOnEntryOffset, kUInt8Size)                                      \
  /* More padding to make the header pointer-size aligned */              \
  V(kHeaderPaddingOffset, POINTER_SIZE_PADDING(kHeaderPaddingOffset))     \
  V(kHeaderSize, 0)                                                       \
  V(kSize, 0)

  DEFINE_FIELD_OFFSET_CONSTANTS(ExposedTrustedObject::kHeaderSize, FIELD_LIST)
  static_assert(IsAligned(kHeaderSize, kTaggedSize));
  // TODO(ishell, v8:8875): When pointer compression is enabled 8-byte size
  // fields (external pointers, doubles and BigInt data) are only kTaggedSize
  // aligned so checking for alignments of fields bigger than kTaggedSize
  // doesn't make sense until v8:8875 is fixed.
#define ASSERT_FIELD_ALIGNED(offset, size)                                 \
  static_assert(size == 0 || IsAligned(offset, size) ||                    \
                (COMPRESS_POINTERS_BOOL && (size == kSystemPointerSize) && \
                 IsAligned(offset, kTaggedSize)));
  FIELD_LIST(ASSERT_FIELD_ALIGNED)
#undef ASSERT_FIELD_ALIGNED
#undef FIELD_LIST

  // GC support: List all tagged fields.
  // V(offset, name)
#define WASM_TAGGED_INSTANCE_OBJECT_FIELDS(V)                                 \
  V(kImportedFunctionRefsOffset, "imported_function_refs")                    \
  V(kIndirectFunctionTableRefsOffset, "indirect_function_table_refs")         \
  V(kIndirectFunctionTableTargetsOffset, "indirect_function_table_targets")   \
  V(kIndirectFunctionTableSigIdsOffset, "indirect_function_table_sig_ids")    \
  V(kInstanceObjectOffset, "instance_object")                                 \
  V(kNativeContextOffset, "native_context")                                   \
  V(kMemoryObjectsOffset, "memory_objects")                                   \
  V(kUntaggedGlobalsBufferOffset, "untagged_globals_buffer")                  \
  V(kTaggedGlobalsBufferOffset, "tagged_globals_buffer")                      \
  V(kImportedMutableGlobalsBuffersOffset, "imported_mutable_globals_buffers") \
  V(kTablesOffset, "tables")                                                  \
  V(kIndirectFunctionTablesOffset, "indirect_function_tables")                \
  V(kTagsTableOffset, "tags_table")                                           \
  V(kWasmInternalFunctionsOffset, "wasm_internal_functions")                  \
  V(kManagedObjectMapsOffset, "managed_object_maps")                          \
  V(kFeedbackVectorsOffset, "feedback_vectors")                               \
  V(kWellKnownImportsOffset, "well_known_imports")                            \
  V(kImportedMutableGlobalsOffset, "imported_mutable_globals")                \
  V(kImportedFunctionTargetsOffset, "imported_function_targets")              \
  V(kMemoryBasesAndSizesOffset, "memory_bases_and_sizes")                     \
  V(kDataSegmentStartsOffset, "data_segment_starts")                          \
  V(kDataSegmentSizesOffset, "data_segment_sizes")                            \
  V(kElementSegmentsOffset, "element_segments")

  static constexpr std::array<uint16_t, 23> kTaggedFieldOffsets = {
#define WASM_INSTANCE_TAGGED_FIELD_OFFSET(offset, _) offset,
      WASM_TAGGED_INSTANCE_OBJECT_FIELDS(WASM_INSTANCE_TAGGED_FIELD_OFFSET)
#undef WASM_INSTANCE_TAGGED_FIELD_OFFSET
  };

  static constexpr std::array<const char*, 23> kTaggedFieldNames = {
#define WASM_INSTANCE_TAGGED_FIELD_NAME(_, name) name,
      WASM_TAGGED_INSTANCE_OBJECT_FIELDS(WASM_INSTANCE_TAGGED_FIELD_NAME)
#undef WASM_INSTANCE_TAGGED_FIELD_NAME
  };
#undef WASM_TAGGED_INSTANCE_OBJECT_FIELDS

  static_assert(kTaggedFieldOffsets.size() == kTaggedFieldNames.size(),
                "every field offset needs a name");

  const wasm::WasmModule* module();

  static bool EnsureIndirectFunctionTableWithMinimumSize(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      int table_index, uint32_t minimum_size);

  void SetRawMemory(int memory_index, uint8_t* mem_start, size_t mem_size);

  static Handle<WasmTrustedInstanceData> New(Isolate*,
                                             Handle<WasmModuleObject>);

  Address GetCallTarget(uint32_t func_index);

  inline Tagged<WasmIndirectFunctionTable> indirect_function_table(
      uint32_t table_index);

  void SetIndirectFunctionTableShortcuts(Isolate* isolate);

  // Copies table entries. Returns {false} if the ranges are out-of-bounds.
  static bool CopyTableEntries(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      uint32_t table_dst_index, uint32_t table_src_index, uint32_t dst,
      uint32_t src, uint32_t count) V8_WARN_UNUSED_RESULT;

  // Loads a range of elements from element segment into a table.
  // Returns the empty {Optional} if the operation succeeds, or an {Optional}
  // with the error {MessageTemplate} if it fails.
  static base::Optional<MessageTemplate> InitTableEntries(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      uint32_t table_index, uint32_t segment_index, uint32_t dst, uint32_t src,
      uint32_t count) V8_WARN_UNUSED_RESULT;

  // Iterates all fields in the object except the untagged fields.
  class BodyDescriptor;

  static MaybeHandle<WasmInternalFunction> GetWasmInternalFunction(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      int index);

  // Acquires the {WasmInternalFunction} for a given {function_index} from the
  // cache of the given {trusted_instance_data}, or creates a new
  // {WasmInternalFunction} if it does not exist yet. The new
  // {WasmInternalFunction} is added to the cache of the {trusted_instance_data}
  // immediately.
  static Handle<WasmInternalFunction> GetOrCreateWasmInternalFunction(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      int function_index);

  static void SetWasmInternalFunction(
      Handle<WasmTrustedInstanceData> trusted_instance_data, int index,
      Handle<WasmInternalFunction> val);

  // Imports a constructed {WasmJSFunction} into the indirect function table of
  // this instance. Note that this might trigger wrapper compilation, since a
  // {WasmJSFunction} is instance-independent and just wraps a JS callable.
  static void ImportWasmJSFunctionIntoTable(
      Isolate* isolate, Handle<WasmTrustedInstanceData> trusted_instance_data,
      int table_index, int entry_index, Handle<WasmJSFunction> js_function);

  // Get a raw pointer to the location where the given global is stored.
  // {global} must not be a reference type.
  uint8_t* GetGlobalStorage(const wasm::WasmGlobal&);

  // Get the FixedArray and the index in that FixedArray for the given global,
  // which must be a reference type.
  std::pair<Tagged<FixedArray>, uint32_t> GetGlobalBufferAndIndex(
      const wasm::WasmGlobal&);

  // Get the value of a global.
  wasm::WasmValue GetGlobalValue(Isolate*, const wasm::WasmGlobal&);

  OBJECT_CONSTRUCTORS(WasmTrustedInstanceData, ExposedTrustedObject);

 private:
  void InitDataSegmentArrays(Tagged<WasmModuleObject>);
};

// Representation of a WebAssembly.Instance JavaScript-level object.
// This is mostly a wrapper around the WasmTrustedInstanceData, plus any
// user-set properties.
class WasmInstanceObject
    : public TorqueGeneratedWasmInstanceObject<WasmInstanceObject, JSObject> {
 public:
  DECL_TRUSTED_POINTER_ACCESSORS(trusted_data, WasmTrustedInstanceData)

  inline const wasm::WasmModule* module() const;

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmInstanceObject)
};

// Representation of WebAssembly.Exception JavaScript-level object.
class WasmTagObject
    : public TorqueGeneratedWasmTagObject<WasmTagObject, JSObject> {
 public:
  // Checks whether the given {sig} has the same parameter types as the
  // serialized signature stored within this tag object.
  bool MatchesSignature(uint32_t expected_canonical_type_index);

  static Handle<WasmTagObject> New(Isolate* isolate,
                                   const wasm::FunctionSig* sig,
                                   uint32_t canonical_type_index,
                                   Handle<HeapObject> tag,
                                   Handle<HeapObject> instance);

  TQ_OBJECT_CONSTRUCTORS(WasmTagObject)
};

// A Wasm exception that has been thrown out of Wasm code.
class V8_EXPORT_PRIVATE WasmExceptionPackage : public JSObject {
 public:
  static Handle<WasmExceptionPackage> New(
      Isolate* isolate, Handle<WasmExceptionTag> exception_tag,
      int encoded_size);

  static Handle<WasmExceptionPackage> New(
      Isolate* isolate, Handle<WasmExceptionTag> exception_tag,
      Handle<FixedArray> values);

  // The below getters return {undefined} in case the given exception package
  // does not carry the requested values (i.e. is of a different type).
  static Handle<Object> GetExceptionTag(
      Isolate* isolate, Handle<WasmExceptionPackage> exception_package);
  static Handle<Object> GetExceptionValues(
      Isolate* isolate, Handle<WasmExceptionPackage> exception_package);

  // Determines the size of the array holding all encoded exception values.
  static uint32_t GetEncodedSize(const wasm::WasmTagSig* tag);
  static uint32_t GetEncodedSize(const wasm::WasmTag* tag);

  // In-object fields.
  enum { kTagIndex, kValuesIndex, kInObjectFieldCount };
  static constexpr int kSize =
      kHeaderSize + (kTaggedSize * kInObjectFieldCount);

  DECL_CAST(WasmExceptionPackage)
  DECL_PRINTER(WasmExceptionPackage)
  DECL_VERIFIER(WasmExceptionPackage)
  OBJECT_CONSTRUCTORS(WasmExceptionPackage, JSObject);
};

void V8_EXPORT_PRIVATE EncodeI32ExceptionValue(
    Handle<FixedArray> encoded_values, uint32_t* encoded_index, uint32_t value);

void V8_EXPORT_PRIVATE EncodeI64ExceptionValue(
    Handle<FixedArray> encoded_values, uint32_t* encoded_index, uint64_t value);

void V8_EXPORT_PRIVATE
DecodeI32ExceptionValue(Handle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint32_t* value);

void V8_EXPORT_PRIVATE
DecodeI64ExceptionValue(Handle<FixedArray> encoded_values,
                        uint32_t* encoded_index, uint64_t* value);

bool UseGenericWasmToJSWrapper(wasm::ImportCallKind kind,
                               const wasm::FunctionSig* sig,
                               wasm::Suspend suspend);

// A Wasm function that is wrapped and exported to JavaScript.
// Representation of WebAssembly.Function JavaScript-level object.
class WasmExportedFunction : public JSFunction {
 public:
  Tagged<WasmInstanceObject> instance();
  V8_EXPORT_PRIVATE int function_index();

  V8_EXPORT_PRIVATE static bool IsWasmExportedFunction(Tagged<Object> object);

  V8_EXPORT_PRIVATE static Handle<WasmExportedFunction> New(
      Isolate* isolate, Handle<WasmInstanceObject> instance_object,
      Handle<WasmInternalFunction> internal, int func_index, int arity,
      Handle<Code> export_wrapper);

  Address GetWasmCallTarget();

  V8_EXPORT_PRIVATE const wasm::FunctionSig* sig();

  bool MatchesSignature(uint32_t other_canonical_sig_index);

  // Return a null-terminated string with the debug name in the form
  // 'js-to-wasm:<sig>'.
  static std::unique_ptr<char[]> GetDebugName(const wasm::FunctionSig* sig);

  DECL_CAST(WasmExportedFunction)
  OBJECT_CONSTRUCTORS(WasmExportedFunction, JSFunction);
};

// A Wasm function that was created by wrapping a JavaScript callable.
// Representation of WebAssembly.Function JavaScript-level object.
class WasmJSFunction : public JSFunction {
 public:
  static bool IsWasmJSFunction(Tagged<Object> object);

  static Handle<WasmJSFunction> New(Isolate* isolate,
                                    const wasm::FunctionSig* sig,
                                    Handle<JSReceiver> callable,
                                    wasm::Suspend suspend);

  Tagged<JSReceiver> GetCallable() const;
  wasm::Suspend GetSuspend() const;
  // Deserializes the signature of this function using the provided zone. Note
  // that lifetime of the signature is hence directly coupled to the zone.
  const wasm::FunctionSig* GetSignature(Zone* zone) const;
  bool MatchesSignature(uint32_t other_canonical_sig_index) const;

  DECL_CAST(WasmJSFunction)
  OBJECT_CONSTRUCTORS(WasmJSFunction, JSFunction);
};

// An external function exposed to Wasm via the C/C++ API.
class WasmCapiFunction : public JSFunction {
 public:
  static bool IsWasmCapiFunction(Tagged<Object> object);

  static Handle<WasmCapiFunction> New(
      Isolate* isolate, Address call_target, Handle<Foreign> embedder_data,
      Handle<PodArray<wasm::ValueType>> serialized_signature);

  Tagged<PodArray<wasm::ValueType>> GetSerializedSignature() const;
  // Checks whether the given {sig} has the same parameter types as the
  // serialized signature stored within this C-API function object.
  bool MatchesSignature(uint32_t other_canonical_sig_index) const;
  const wasm::FunctionSig* GetSignature(Zone* zone) const;

  DECL_CAST(WasmCapiFunction)
  OBJECT_CONSTRUCTORS(WasmCapiFunction, JSFunction);
};

// Any external function that can be imported/exported in modules. This abstract
// class just dispatches to the following concrete classes:
//  - {WasmExportedFunction}: A proper Wasm function exported from a module.
//  - {WasmJSFunction}: A function constructed via WebAssembly.Function in JS.
// // TODO(wasm): Potentially {WasmCapiFunction} will be added here as well.
class WasmExternalFunction : public JSFunction {
 public:
  static bool IsWasmExternalFunction(Tagged<Object> object);

  DECL_CAST(WasmExternalFunction)
  OBJECT_CONSTRUCTORS(WasmExternalFunction, JSFunction);
};

class WasmIndirectFunctionTable
    : public TorqueGeneratedWasmIndirectFunctionTable<WasmIndirectFunctionTable,
                                                      Struct> {
 public:
  DECL_ACCESSORS(sig_ids, Tagged<FixedUInt32Array>)
  // When the sandbox is enabled, this array holds indices into the external
  // pointer table that contain the function entrypoint. Otherwise, this array
  // directly contains the entrypoint pointers.
  // TODO(chromium:1395058): consider instead turning this entire structure into
  // a pointer table entry. For example, we could create a WasmCodePointerTable
  // where each entry contains the signature, the target, and the ref
  // object. Then the WasmIndirectFunctionTable object would simply contain an
  // array of indices into a WasmCodePointerTable. This way, we can also
  // guarantee that an attacker cannot for example modify the signature
  // associated with a target function.
  DECL_ACCESSORS(targets, Tagged<ExternalPointerArray>)

  V8_EXPORT_PRIVATE static Handle<WasmIndirectFunctionTable> New(
      Isolate* isolate, uint32_t size);
  static void Resize(Isolate* isolate, Handle<WasmIndirectFunctionTable> table,
                     uint32_t new_size);
  V8_EXPORT_PRIVATE void Set(uint32_t index, int sig_id, Address call_target,
                             Tagged<Object> ref);
  void Clear(uint32_t index);

  DECL_PRINTER(WasmIndirectFunctionTable)

  using BodyDescriptor =
      FixedBodyDescriptor<kStartOfStrongFieldsOffset, kEndOfStrongFieldsOffset,
                          kHeaderSize>;

  TQ_OBJECT_CONSTRUCTORS(WasmIndirectFunctionTable)
};

class WasmFunctionData
    : public TorqueGeneratedWasmFunctionData<WasmFunctionData, HeapObject> {
 public:
  DECL_CODE_POINTER_ACCESSORS(wrapper_code)

  DECL_ACCESSORS(internal, Tagged<WasmInternalFunction>)

  DECL_PRINTER(WasmFunctionData)

  using BodyDescriptor = FixedBodyDescriptor<kStartOfStrongFieldsOffset,
                                             kEndOfStrongFieldsOffset, kSize>;

  using SuspendField = base::BitField<wasm::Suspend, 0, 1>;
  using PromiseField = base::BitField<wasm::Promise, 1, 1>;

  TQ_OBJECT_CONSTRUCTORS(WasmFunctionData)
};

// Information for a WasmExportedFunction which is referenced as the function
// data of the SharedFunctionInfo underlying the function. For details please
// see the {SharedFunctionInfo::HasWasmExportedFunctionData} predicate.
class WasmExportedFunctionData
    : public TorqueGeneratedWasmExportedFunctionData<WasmExportedFunctionData,
                                                     WasmFunctionData> {
 public:
  DECL_CODE_POINTER_ACCESSORS(c_wrapper_code)

  DECL_EXTERNAL_POINTER_ACCESSORS(sig, wasm::FunctionSig*)

  // Dispatched behavior.
  DECL_PRINTER(WasmExportedFunctionData)
  DECL_VERIFIER(WasmExportedFunctionData)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmExportedFunctionData)
};

class WasmApiFunctionRef
    : public TorqueGeneratedWasmApiFunctionRef<WasmApiFunctionRef, HeapObject> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(WasmApiFunctionRef)
  static constexpr int kInvalidCallOrigin = 0;

  static void SetImportIndexAsCallOrigin(Handle<WasmApiFunctionRef> ref,
                                         int entry_index);

  static bool CallOriginIsImportIndex(Handle<Object> call_origin);

  static bool CallOriginIsIndexInTable(Handle<Object> call_origin);

  static int CallOriginAsIndex(Handle<Object> call_origin);

  static void SetIndexInTableAsCallOrigin(Handle<WasmApiFunctionRef> ref,
                                          int entry_index);

  static void SetCrossInstanceTableIndexAsCallOrigin(
      Isolate* isolate, Handle<WasmApiFunctionRef> ref,
      Handle<WasmInstanceObject> instance_object, int entry_index);

  static void SetInternalFunctionAsCallOrigin(
      Handle<WasmApiFunctionRef> ref, Handle<WasmInternalFunction> internal);

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmApiFunctionRef)
};

class WasmInternalFunction
    : public TorqueGeneratedWasmInternalFunction<WasmInternalFunction,
                                                 HeapObject> {
 public:
  // Returns a handle to the corresponding WasmInternalFunction if {external} is
  // a WasmExternalFunction, or an empty handle otherwise.
  static MaybeHandle<WasmInternalFunction> FromExternal(Handle<Object> external,
                                                        Isolate* isolate);

  V8_EXPORT_PRIVATE static Handle<JSFunction> GetOrCreateExternal(
      Handle<WasmInternalFunction> internal);

  DECL_EXTERNAL_POINTER_ACCESSORS(call_target, Address)

  DECL_CODE_POINTER_ACCESSORS(code)

  // Dispatched behavior.
  DECL_PRINTER(WasmInternalFunction)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmInternalFunction)
 private:
  // Make this private so it is not use by accident. Use {GetOrCreateExternal}
  // instead.
  Tagged<HeapObject> external();
};

// Information for a WasmJSFunction which is referenced as the function data of
// the SharedFunctionInfo underlying the function. For details please see the
// {SharedFunctionInfo::HasWasmJSFunctionData} predicate.
class WasmJSFunctionData
    : public TorqueGeneratedWasmJSFunctionData<WasmJSFunctionData,
                                               WasmFunctionData> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(WasmJSFunctionData)

  using BodyDescriptor =
      FlexibleBodyDescriptor<WasmFunctionData::kStartOfStrongFieldsOffset>;

 private:
  TQ_OBJECT_CONSTRUCTORS(WasmJSFunctionData)
};

class WasmCapiFunctionData
    : public TorqueGeneratedWasmCapiFunctionData<WasmCapiFunctionData,
                                                 WasmFunctionData> {
 public:
  DECL_PRINTER(WasmCapiFunctionData)

  using BodyDescriptor =
      FlexibleBodyDescriptor<WasmFunctionData::kStartOfStrongFieldsOffset>;

  TQ_OBJECT_CONSTRUCTORS(WasmCapiFunctionData)
};

class WasmResumeData
    : public TorqueGeneratedWasmResumeData<WasmResumeData, HeapObject> {
 public:
  using BodyDescriptor =
      FlexibleBodyDescriptor<WasmResumeData::kStartOfStrongFieldsOffset>;
  DECL_PRINTER(WasmResumeData)
  TQ_OBJECT_CONSTRUCTORS(WasmResumeData)
};

class WasmScript : public AllStatic {
 public:
  // Position used for storing "on entry" breakpoints (a.k.a. instrumentation
  // breakpoints). This would be an illegal position for any other breakpoint.
  static constexpr int kOnEntryBreakpointPosition = -1;

  // Set a breakpoint on the given byte position inside the given module.
  // This will affect all live and future instances of the module.
  // The passed position might be modified to point to the next breakable
  // location inside the same function.
  // If it points outside a function, or behind the last breakable location,
  // this function returns false and does not set any breakpoint.
  V8_EXPORT_PRIVATE static bool SetBreakPoint(Handle<Script>, int* position,
                                              Handle<BreakPoint> break_point);

  // Set an "on entry" breakpoint (a.k.a. instrumentation breakpoint) inside
  // the given module. This will affect all live and future instances of the
  // module.
  V8_EXPORT_PRIVATE static void SetInstrumentationBreakpoint(
      Handle<Script>, Handle<BreakPoint> break_point);

  // Set a breakpoint on first breakable position of the given function index
  // inside the given module. This will affect all live and future instances of
  // the module.
  V8_EXPORT_PRIVATE static bool SetBreakPointOnFirstBreakableForFunction(
      Handle<Script>, int function_index, Handle<BreakPoint> break_point);

  // Set a breakpoint at the breakable offset of the given function index
  // inside the given module. This will affect all live and future instances of
  // the module.
  V8_EXPORT_PRIVATE static bool SetBreakPointForFunction(
      Handle<Script>, int function_index, int breakable_offset,
      Handle<BreakPoint> break_point);

  // Remove a previously set breakpoint at the given byte position inside the
  // given module. If this breakpoint is not found this function returns false.
  V8_EXPORT_PRIVATE static bool ClearBreakPoint(Handle<Script>, int position,
                                                Handle<BreakPoint> break_point);

  // Remove a previously set breakpoint by id. If this breakpoint is not found,
  // returns false.
  V8_EXPORT_PRIVATE static bool ClearBreakPointById(Handle<Script>,
                                                    int breakpoint_id);

  // Remove all set breakpoints.
  static void ClearAllBreakpoints(Tagged<Script>);

  // Get a list of all possible breakpoints within a given range of this module.
  V8_EXPORT_PRIVATE static bool GetPossibleBreakpoints(
      wasm::NativeModule* native_module, const debug::Location& start,
      const debug::Location& end, std::vector<debug::BreakLocation>* locations);

  // Return an empty handle if no breakpoint is hit at that location, or a
  // FixedArray with all hit breakpoint objects.
  static MaybeHandle<FixedArray> CheckBreakPoints(Isolate*, Handle<Script>,
                                                  int position,
                                                  StackFrameId stack_frame_id);

 private:
  // Helper functions that update the breakpoint info list.
  static void AddBreakpointToInfo(Handle<Script>, int position,
                                  Handle<BreakPoint> break_point);
};

// Tags provide an object identity for each exception defined in a wasm module
// header. They are referenced by the following fields:
//  - {WasmTagObject::tag}: The tag of the {Tag} object.
//  - {WasmInstanceObject::tags_table}: List of tags used by an instance.
class WasmExceptionTag
    : public TorqueGeneratedWasmExceptionTag<WasmExceptionTag, Struct> {
 public:
  V8_EXPORT_PRIVATE static Handle<WasmExceptionTag> New(Isolate* isolate,
                                                        int index);

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmExceptionTag)
};

// Data annotated to the asm.js Module function. Used for later instantiation of
// that function.
class AsmWasmData : public TorqueGeneratedAsmWasmData<AsmWasmData, Struct> {
 public:
  static Handle<AsmWasmData> New(
      Isolate* isolate, std::shared_ptr<wasm::NativeModule> native_module,
      Handle<HeapNumber> uses_bitset);

  DECL_PRINTER(AsmWasmData)

  using BodyDescriptor = StructBodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(AsmWasmData)
};

class WasmTypeInfo
    : public TorqueGeneratedWasmTypeInfo<WasmTypeInfo, HeapObject> {
 public:
  DECL_EXTERNAL_POINTER_ACCESSORS(native_type, Address)

  DECL_PRINTER(WasmTypeInfo)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmTypeInfo)
};

class WasmObject : public TorqueGeneratedWasmObject<WasmObject, JSReceiver> {
 protected:
  // Returns boxed value of the object's field/element with given type and
  // offset.
  static inline Handle<Object> ReadValueAt(Isolate* isolate,
                                           Handle<HeapObject> obj,
                                           wasm::ValueType type,
                                           uint32_t offset);
 private:
  template <typename ElementType>
  static ElementType FromNumber(Tagged<Object> value);

  TQ_OBJECT_CONSTRUCTORS(WasmObject)
};

class WasmStruct : public TorqueGeneratedWasmStruct<WasmStruct, WasmObject> {
 public:
  static inline wasm::StructType* type(Tagged<Map> map);
  inline wasm::StructType* type() const;
  static inline wasm::StructType* GcSafeType(Tagged<Map> map);
  static inline int Size(const wasm::StructType* type);
  static inline int GcSafeSize(Tagged<Map> map);
  static inline void EncodeInstanceSizeInMap(int instance_size,
                                             Tagged<Map> map);
  static inline int DecodeInstanceSizeFromMap(Tagged<Map> map);

  // Returns the address of the field at given offset.
  inline Address RawFieldAddress(int raw_offset);

  // Returns the ObjectSlot for tagged value at given offset.
  inline ObjectSlot RawField(int raw_offset);

  wasm::WasmValue GetFieldValue(uint32_t field_index);

  static inline void SetField(Isolate* isolate, Handle<WasmStruct> obj,
                              uint32_t field_index, Handle<Object> value);

  DECL_PRINTER(WasmStruct)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmStruct)
};

int WasmStruct::Size(const wasm::StructType* type) {
  // Object size must fit into a Smi (because of filler objects), and its
  // computation must not overflow.
  static_assert(Smi::kMaxValue <= kMaxInt);
  DCHECK_LE(type->total_fields_size(), Smi::kMaxValue - kHeaderSize);
  return std::max(kHeaderSize + static_cast<int>(type->total_fields_size()),
                  Heap::kMinObjectSizeInTaggedWords * kTaggedSize);
}

class WasmArray : public TorqueGeneratedWasmArray<WasmArray, WasmObject> {
 public:
  static inline wasm::ArrayType* type(Tagged<Map> map);
  inline wasm::ArrayType* type() const;
  static inline wasm::ArrayType* GcSafeType(Tagged<Map> map);

  // Get the {ObjectSlot} corresponding to the element at {index}. Requires that
  // this is a reference array.
  inline ObjectSlot ElementSlot(uint32_t index);
  V8_EXPORT_PRIVATE wasm::WasmValue GetElement(uint32_t index);

  static inline int SizeFor(Tagged<Map> map, int length);

  // Returns boxed value of the array's element.
  static inline Handle<Object> GetElement(Isolate* isolate,
                                          Handle<WasmArray> array,
                                          uint32_t index);

  void SetTaggedElement(uint32_t index, Handle<Object> value,
                        WriteBarrierMode mode = UPDATE_WRITE_BARRIER);

  // Returns the offset/Address of the element at {index}.
  inline uint32_t element_offset(uint32_t index);
  inline Address ElementAddress(uint32_t index);

  static constexpr int MaxLength(uint32_t element_size_bytes) {
    // The total object size must fit into a Smi, for filler objects. To make
    // the behavior of Wasm programs independent from the Smi configuration,
    // we hard-code the smaller of the two supported ranges.
    return (SmiTagging<4>::kSmiMaxValue - kHeaderSize) / element_size_bytes;
  }

  static int MaxLength(const wasm::ArrayType* type) {
    return MaxLength(type->element_type().value_kind_size());
  }

  static inline void EncodeElementSizeInMap(int element_size, Tagged<Map> map);
  static inline int DecodeElementSizeFromMap(Tagged<Map> map);

  DECL_PRINTER(WasmArray)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmArray)
};

// A wasm delimited continuation.
class WasmContinuationObject
    : public TorqueGeneratedWasmContinuationObject<WasmContinuationObject,
                                                   HeapObject> {
 public:
  static Handle<WasmContinuationObject> New(
      Isolate* isolate, std::unique_ptr<wasm::StackMemory> stack,
      wasm::JumpBuffer::StackState state,
      AllocationType allocation_type = AllocationType::kYoung);
  static Handle<WasmContinuationObject> New(
      Isolate* isolate, wasm::JumpBuffer::StackState state,
      Handle<WasmContinuationObject> parent);

  DECL_EXTERNAL_POINTER_ACCESSORS(jmpbuf, Address)

  DECL_PRINTER(WasmContinuationObject)

  class BodyDescriptor;

 private:
  static Handle<WasmContinuationObject> New(
      Isolate* isolate, std::unique_ptr<wasm::StackMemory> stack,
      wasm::JumpBuffer::StackState state, Handle<HeapObject> parent,
      AllocationType allocation_type = AllocationType::kYoung);

  TQ_OBJECT_CONSTRUCTORS(WasmContinuationObject)
};

// The suspender object provides an API to suspend and resume wasm code using
// promises. See: https://github.com/WebAssembly/js-promise-integration.
class WasmSuspenderObject
    : public TorqueGeneratedWasmSuspenderObject<WasmSuspenderObject, JSObject> {
 public:
  enum State : int { kInactive = 0, kActive, kSuspended };
  static Handle<WasmSuspenderObject> New(Isolate* isolate);
  DECL_PRINTER(WasmSuspenderObject)
  using BodyDescriptor =
      FixedBodyDescriptor<kStartOfStrongFieldsOffset, kEndOfStrongFieldsOffset,
                          kHeaderSize>;
  TQ_OBJECT_CONSTRUCTORS(WasmSuspenderObject)
};

class WasmNull : public TorqueGeneratedWasmNull<WasmNull, HeapObject> {
 public:
#if V8_STATIC_ROOTS_BOOL || V8_STATIC_ROOTS_GENERATION_BOOL
  // TODO(manoskouk): Make it smaller if able and needed.
  static constexpr int kSize = 64 * KB + kTaggedSize;
  // Payload should be a multiple of page size.
  static_assert((kSize - kTaggedSize) % kMinimumOSPageSize == 0);
  // Any wasm struct offset should fit in the object.
  static_assert(kSize >=
                WasmStruct::kHeaderSize +
                    (wasm::kMaxStructFieldIndexForImplicitNullCheck + 1) *
                        kSimd128Size);

  Address payload() { return ptr() + kHeaderSize - kHeapObjectTag; }
  static constexpr size_t kPayloadSize = kSize - kTaggedSize;
#else
  static constexpr int kSize = kTaggedSize;
#endif
  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(WasmNull)
};

#undef DECL_OPTIONAL_ACCESSORS

Handle<Map> CreateFuncRefMap(Isolate* isolate, Handle<Map> opt_rtt_parent);

namespace wasm {
// Takes a {value} in the JS representation and typechecks it according to
// {expected}. If the typecheck succeeds, returns the wasm representation of the
// object; otherwise, returns the empty handle.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, Handle<Object> value,
                                   ValueType expected_canonical,
                                   const char** error_message);

// Utility which canonicalizes {expected} in addition.
MaybeHandle<Object> JSToWasmObject(Isolate* isolate, const WasmModule* module,
                                   Handle<Object> value, ValueType expected,
                                   const char** error_message);

// Takes a {value} in the Wasm representation and transforms it to the
// respective JS representation. The caller is responsible for not providing an
// object which cannot be transformed to JS.
Handle<Object> WasmToJSObject(Isolate* isolate, Handle<Object> value);
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_WASM_WASM_OBJECTS_H_
