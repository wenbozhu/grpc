/*
 *
 * Copyright 2016, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <uv.h>
#include <node.h>
#include <v8.h>
#include <grpc/grpc.h>

#include "call.h"
#include "completion_queue.h"
#include "completion_queue_async_worker.h"

namespace grpc {
namespace node {

using v8::Local;
using v8::Object;
using v8::Value;

grpc_completion_queue *queue;
uv_prepare_t prepare;
int pending_batches;

void drain_completion_queue(uv_prepare_t *handle) {
  Nan::HandleScope scope;
  grpc_event event;
  (void)handle;
  do {
    event = grpc_completion_queue_next(
        queue, gpr_inf_past(GPR_CLOCK_MONOTONIC), NULL);

    if (event.type == GRPC_OP_COMPLETE) {
      Nan::Callback *callback = grpc::node::GetTagCallback(event.tag);
      if (event.success) {
        Local<Value> argv[] = {Nan::Null(),
                             grpc::node::GetTagNodeValue(event.tag)};
        callback->Call(2, argv);
      } else {
        Local<Value> argv[] = {Nan::Error(
            "The async function encountered an error")};
        callback->Call(1, argv);
      }
      grpc::node::CompleteTag(event.tag);
      grpc::node::DestroyTag(event.tag);
      pending_batches--;
      if (pending_batches == 0) {
        uv_prepare_stop(&prepare);
      }
    }
  } while (event.type != GRPC_QUEUE_TIMEOUT);
}

grpc_completion_queue *GetCompletionQueue() {
#ifdef GRPC_UV
  return queue;
#else
  return CompletionQueueAsyncWorker::GetQueue();
#endif
}

void CompletionQueueNext() {
#ifdef GRPC_UV
  if (pending_batches == 0) {
    GPR_ASSERT(!uv_is_active((uv_handle_t *)&prepare));
    uv_prepare_start(&prepare, drain_completion_queue);
  }
  pending_batches++;
#else
  CompletionQueueAsyncWorker::Next();
#endif
}

void CompletionQueueInit(Local<Object> exports) {
#ifdef GRPC_UV
  queue = grpc_completion_queue_create(NULL);
  uv_prepare_init(uv_default_loop(), &prepare);
  pending_batches = 0;
#else
  CompletionQueueAsyncWorker::Init(exports);
#endif
}

}  // namespace node
}  // namespace grpc
