// Copyright 2018 Ryan Dahl <ry@tinyclouds.org>
// All rights reserved. MIT License.
#include "testing/gtest/include/gtest/gtest.h"

#include "deno.h"

TEST(MockRuntimeTest, InitializesCorrectly) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js", "1 + 2"));
  deno_delete(d);
}

TEST(MockRuntimeTest, CanCallFunction) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js",
                           "if (CanCallFunction() != 'foo') throw Error();"));
  deno_delete(d);
}

TEST(MockRuntimeTest, ErrorsCorrectly) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_FALSE(deno_execute(d, "a.js", "throw Error()"));
  deno_delete(d);
}

deno_buf strbuf(const char* str) {
  auto len = strlen(str);

  deno_buf buf;
  buf.alloc_ptr = reinterpret_cast<uint8_t*>(strdup(str));
  buf.alloc_len = len + 1;
  buf.data_ptr = buf.alloc_ptr;
  buf.data_len = len;

  return buf;
}

TEST(MockRuntimeTest, SendSuccess) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js", "SendSuccess()"));
  EXPECT_TRUE(deno_send(d, strbuf("abc")));
  deno_delete(d);
}

TEST(MockRuntimeTest, SendByteLength) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js", "SendByteLength()"));
  // We pub the wrong sized message, it should throw.
  EXPECT_FALSE(deno_send(d, strbuf("abcd")));
  deno_delete(d);
}

TEST(MockRuntimeTest, SendNoCallback) {
  Deno* d = deno_new(nullptr, nullptr);
  // We didn't call deno.recv() in JS, should fail.
  EXPECT_FALSE(deno_send(d, strbuf("abc")));
  deno_delete(d);
}

TEST(MockRuntimeTest, RecvReturnEmpty) {
  static int count = 0;
  Deno* d = deno_new(nullptr, [](auto _, auto buf) {
    count++;
    EXPECT_EQ(static_cast<size_t>(3), buf.data_len);
    EXPECT_EQ(buf.data_ptr[0], 'a');
    EXPECT_EQ(buf.data_ptr[1], 'b');
    EXPECT_EQ(buf.data_ptr[2], 'c');
  });
  EXPECT_TRUE(deno_execute(d, "a.js", "RecvReturnEmpty()"));
  EXPECT_EQ(count, 2);
  deno_delete(d);
}

TEST(MockRuntimeTest, RecvReturnBar) {
  static int count = 0;
  Deno* d = deno_new(nullptr, [](auto deno, auto buf) {
    count++;
    EXPECT_EQ(static_cast<size_t>(3), buf.data_len);
    EXPECT_EQ(buf.data_ptr[0], 'a');
    EXPECT_EQ(buf.data_ptr[1], 'b');
    EXPECT_EQ(buf.data_ptr[2], 'c');
    deno_set_response(deno, strbuf("bar"));
  });
  EXPECT_TRUE(deno_execute(d, "a.js", "RecvReturnBar()"));
  EXPECT_EQ(count, 1);
  deno_delete(d);
}

TEST(MockRuntimeTest, DoubleRecvFails) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_FALSE(deno_execute(d, "a.js", "DoubleRecvFails()"));
  deno_delete(d);
}

TEST(MockRuntimeTest, TypedArraySnapshots) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js", "TypedArraySnapshots()"));
  deno_delete(d);
}

TEST(MockRuntimeTest, SnapshotBug) {
  Deno* d = deno_new(nullptr, nullptr);
  EXPECT_TRUE(deno_execute(d, "a.js", "SnapshotBug()"));
  deno_delete(d);
}

TEST(MockRuntimeTest, ErrorHandling) {
  static int count = 0;
  Deno* d = deno_new(nullptr, [](auto deno, auto buf) {
    count++;
    EXPECT_EQ(static_cast<size_t>(1), buf.data_len);
    EXPECT_EQ(buf.data_ptr[0], 42);
  });
  EXPECT_FALSE(deno_execute(d, "a.js", "ErrorHandling()"));
  EXPECT_EQ(count, 1);
  deno_delete(d);
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  deno_init();
  deno_set_flags(&argc, argv);
  return RUN_ALL_TESTS();
}
