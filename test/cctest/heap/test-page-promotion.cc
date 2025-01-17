// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/heap/spaces-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

// Tests don't work when --optimize-for-size is set.
#ifndef V8_LITE_MODE

namespace {

v8::Isolate* NewIsolateForPagePromotion(int min_semi_space_size = 8,
                                        int max_semi_space_size = 8) {
  // Parallel evacuation messes with fragmentation in a way that objects that
  // should be copied in semi space are promoted to old space because of
  // fragmentation.
  FLAG_parallel_compaction = false;
  FLAG_page_promotion = true;
  FLAG_page_promotion_threshold = 0;
  // Parallel scavenge introduces too much fragmentation.
  FLAG_parallel_scavenge = false;
  FLAG_min_semi_space_size = min_semi_space_size;
  // We cannot optimize for size as we require a new space with more than one
  // page.
  FLAG_optimize_for_size = false;
  // Set max_semi_space_size because it could've been initialized by an
  // implication of optimize_for_size.
  FLAG_max_semi_space_size = max_semi_space_size;
  v8::Isolate::CreateParams create_params;
  create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
  v8::Isolate* isolate = v8::Isolate::New(create_params);
  return isolate;
}

Page* FindLastPageInNewSpace(const std::vector<Handle<FixedArray>>& handles) {
  for (auto rit = handles.rbegin(); rit != handles.rend(); ++rit) {
    // One deref gets the Handle, the second deref gets the FixedArray.
    Page* candidate = Page::FromHeapObject(**rit);
    if (candidate->InNewSpace()) return candidate;
  }
  return nullptr;
}

}  // namespace

UNINITIALIZED_TEST(PagePromotion_NewToOld) {
  if (i::FLAG_single_generation) return;
  if (!i::FLAG_incremental_marking) return;
  if (!i::FLAG_page_promotion) return;
  ManualGCScope manual_gc_scope;

  v8::Isolate* isolate = NewIsolateForPagePromotion();
  i::Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Context::New(isolate)->Enter();
    Heap* heap = i_isolate->heap();

    // Ensure that the new space is empty so that the page to be promoted
    // does not contain the age mark.
    heap->CollectGarbage(NEW_SPACE, i::GarbageCollectionReason::kTesting);
    heap->CollectGarbage(NEW_SPACE, i::GarbageCollectionReason::kTesting);

    std::vector<Handle<FixedArray>> handles;
    heap::SimulateFullSpace(heap->new_space(), &handles);
    heap->CollectGarbage(NEW_SPACE, i::GarbageCollectionReason::kTesting);
    CHECK_GT(handles.size(), 0u);
    Page* const to_be_promoted_page = FindLastPageInNewSpace(handles);
    CHECK_NOT_NULL(to_be_promoted_page);
    CHECK(!to_be_promoted_page->Contains(
        SemiSpaceNewSpace::From(heap->new_space())->age_mark()));
    // To perform a sanity check on live bytes we need to mark the heap.
    heap::SimulateIncrementalMarking(heap, true);
    // Sanity check that the page meets the requirements for promotion.
    const int threshold_bytes = static_cast<int>(
        FLAG_page_promotion_threshold *
        MemoryChunkLayout::AllocatableMemoryInDataPage() / 100);
    CHECK_GE(heap->incremental_marking()->marking_state()->live_bytes(
                 to_be_promoted_page),
             threshold_bytes);

    // Actual checks: The page is in new space first, but is moved to old space
    // during a full GC.
    CHECK(heap->new_space()->ContainsSlow(to_be_promoted_page->address()));
    CHECK(!heap->old_space()->ContainsSlow(to_be_promoted_page->address()));
    heap::GcAndSweep(heap, OLD_SPACE);
    CHECK(!heap->new_space()->ContainsSlow(to_be_promoted_page->address()));
    CHECK(heap->old_space()->ContainsSlow(to_be_promoted_page->address()));
  }
  isolate->Dispose();
}

#endif  // V8_LITE_MODE

}  // namespace heap
}  // namespace internal
}  // namespace v8
