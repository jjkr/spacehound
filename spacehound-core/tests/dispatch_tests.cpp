// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <CoreFoundation/CoreFoundation.h>

#include <atomic>

#include <spacehound/dispatch.hpp>

namespace {

struct callback_context final {
  std::atomic<int> *count = nullptr;
};

void increment_count(void *context) {
  auto *callback = static_cast<callback_context *>(context);
  callback->count->fetch_add(1, std::memory_order_relaxed);
}

TEST(dispatch_tests, to_main_dispatches_a_function_to_the_main_queue) {
  std::atomic<int> count = 0;
  callback_context context{.count = &count};

  spacehound::dispatch::to_main(&increment_count, &context);

  for (int attempt = 0; attempt < 100 && count.load(std::memory_order_relaxed) == 0;
       ++attempt) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
  }

  EXPECT_EQ(count.load(std::memory_order_relaxed), 1);
}

}  // namespace
