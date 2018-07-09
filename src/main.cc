// Copyright 2018 Ryan Dahl <ry@tinyclouds.org>
// All rights reserved. MIT License.
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "deno.h"
#include "flatbuffers/flatbuffers.h"
#include "src/handlers.h"
#include "src/msg_generated.h"
#include "third_party/v8/src/base/logging.h"

namespace deno {

static char** global_argv;
static int global_argc;

// Wrap the default FlatBufferBuilder class, because the default one can't give
// us a pointer to the output buffer that we own. Nominally,
// FlatBufferBuilder::Release() should do that, but it returns some
// smart-pointer-like object (DetachedBuffer) that frees the buffer when it goes
// out of scope.
//
// This wrapper adds the `ExportBuf` method that returns a deno_buf, which
// is really not owned at all -- the caller is responsible for releasing the
// allocation with free().
//
// The alternative allocator also uses malloc()/free(), rather than
// new/delete[], so that the exported buffer can be later be converted to an
// ArrayBuffer; the (default) V8 ArrayBuffer allocator also uses free().
class FlatBufferBuilder : public flatbuffers::FlatBufferBuilder {
  static const size_t kDefaultInitialSize = 1024;

  class Allocator : public flatbuffers::Allocator {
    uint8_t* keep_alloc_ptr_ = nullptr;
    uint8_t* last_alloc_ptr_ = nullptr;
    size_t last_alloc_len_ = 0;

   public:
    uint8_t* allocate(size_t size) {
      auto ptr = reinterpret_cast<uint8_t*>(malloc(size));
      if (ptr == nullptr) {
        return nullptr;
      }

      last_alloc_ptr_ = ptr;
      last_alloc_len_ = size;

      return ptr;
    }

    void deallocate(uint8_t* ptr, size_t size) {
      if (ptr == last_alloc_ptr_) {
        last_alloc_ptr_ = nullptr;
        last_alloc_len_ = 0;
      }

      if (ptr == keep_alloc_ptr_) {
        // This allocation became an exported buffer, so don't free it.
        // Clearing keep_alloc_ptr_ makes it possible to export another
        // buffer later (after the builder is reset with `Reset()`).
        keep_alloc_ptr_ = nullptr;
        return;
      }

      free(ptr);
    }

    deno_buf GetAndKeepBuf(uint8_t* data_ptr, size_t data_len) {
      // The builder will typically allocate one chunk of memory with some
      // default size. After that, it'll only call allocate() again if the
      // initial allocation wasn't big enough, which is then immediately
      // followed by deallocate() to release the buffer that was too small.
      //
      // Therefore we can assume that the `data_ptr` points somewhere inside
      // the last allocation, and that we never have to protect earlier
      // allocations from being released.
      //
      // Each builder gets it's own Allocator instance, so multiple builders
      // can be exist at the same time without conflicts.

      assert(last_alloc_ptr_ != nullptr);   // Must have allocated.
      assert(keep_alloc_ptr_ == nullptr);   // Didn't export any buffer so far.
      assert(data_ptr >= last_alloc_ptr_);  // Data must be within allocation.
      assert(data_ptr + data_len <= last_alloc_ptr_ + last_alloc_len_);

      keep_alloc_ptr_ = last_alloc_ptr_;

      deno_buf buf;
      buf.alloc_ptr = last_alloc_ptr_;
      buf.alloc_len = last_alloc_len_;
      buf.data_ptr = data_ptr;
      buf.data_len = data_len;
      return buf;
    }
  };

  Allocator allocator_;

 public:
  FlatBufferBuilder()
      : flatbuffers::FlatBufferBuilder(kDefaultInitialSize, &allocator_){};

  // Export the finalized flatbuffer as a deno_buf structure. The caller takes
  // ownership of the underlying memory allocation, which must be released with
  // free().
  // Afer calling ExportBuf() the FlatBufferBuilder should no longer be used;
  // However it can be used again once it is reset with the Reset() method.
  deno_buf ExportBuf() {
    uint8_t* data_ptr = GetBufferPointer();
    size_t data_len = GetSize();
    return allocator_.GetAndKeepBuf(data_ptr, data_len);
  }
};

// Sends StartRes message
void HandleStart(Deno* d, uint32_t cmd_id) {
  FlatBufferBuilder builder;

  char cwdbuf[1024];
  // TODO(piscisaureus): support unicode on windows.
  getcwd(cwdbuf, sizeof(cwdbuf));
  auto start_cwd = builder.CreateString(cwdbuf);

  std::vector<flatbuffers::Offset<flatbuffers::String>> args;
  for (int i = 0; i < global_argc; ++i) {
    args.push_back(builder.CreateString(global_argv[i]));
  }

  auto start_argv = builder.CreateVector(args);
  auto start_msg = CreateStartRes(builder, start_cwd, start_argv);
  auto base = CreateBase(builder, cmd_id, 0, Any_StartRes, start_msg.Union());
  builder.Finish(base);
  deno_set_response(d, builder.ExportBuf());
}

void HandleCodeFetch(Deno* d, uint32_t cmd_id, const CodeFetch* msg) {
  auto module_specifier = msg->module_specifier()->c_str();
  auto containing_file = msg->containing_file()->c_str();
  printf("HandleCodeFetch module_specifier = %s containing_file = %s\n",
         module_specifier, containing_file);
  // Call into rust.
  handle_code_fetch(cmd_id, module_specifier, containing_file);
}

void MessagesFromJS(Deno* d, deno_buf buf) {
  flatbuffers::Verifier verifier(buf.data_ptr, buf.data_len);
  DCHECK(verifier.VerifyBuffer<Base>());

  auto base = flatbuffers::GetRoot<Base>(buf.data_ptr);
  auto cmd_id = base->cmdId();
  auto msg_type = base->msg_type();
  const char* msg_type_name = EnumNamesAny()[msg_type];
  printf("MessagesFromJS cmd_id = %d, msg_type = %d, msg_type_name = %s\n",
         cmd_id, msg_type, msg_type_name);
  switch (msg_type) {
    case Any_Start:
      HandleStart(d, base->cmdId());
      break;

    case Any_CodeFetch:
      HandleCodeFetch(d, base->cmdId(), base->msg_as_CodeFetch());
      break;

    case Any_NONE:
      CHECK(false && "Got message with msg_type == Any_NONE");
      break;

    default:
      printf("Unhandled message %s\n", msg_type_name);
      CHECK(false && "Unhandled message");
      break;
  }
}

int deno_main(int argc, char** argv) {
  deno_init();

  deno_set_flags(&argc, argv);
  global_argv = argv;
  global_argc = argc;

  Deno* d = deno_new(NULL, MessagesFromJS);
  bool r = deno_execute(d, "deno_main.js", "denoMain();");
  if (!r) {
    printf("%s\n", deno_last_exception(d));
    exit(1);
  }
  deno_delete(d);
  return 0;
}

}  // namespace deno

int main(int argc, char** argv) { return deno::deno_main(argc, argv); }
