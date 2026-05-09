#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include <l3_external_msgs/msg/tracked_target_array.hpp>
#include <l3_msgs/msg/tracked_target.hpp>

#include "m2_world_model/track_buffer.hpp"

namespace mass_l3::m2 {
namespace {

using time_point = std::chrono::steady_clock::time_point;

// ── Test helpers ─────────────────────────────────────────────────────────────

auto make_msg(const std::vector<uint64_t>& ids) {
  l3_external_msgs::msg::TrackedTargetArray msg;
  for (const auto id : ids) {
    l3_msgs::msg::TrackedTarget tgt;
    tgt.target_id = id;
    tgt.position.latitude = 35.0;
    tgt.position.longitude = 139.0;
    tgt.sog_kn = 10.0;
    tgt.cog_deg = 45.0;
    tgt.heading_deg = 40.0;
    tgt.classification = "cargo";
    tgt.classification_confidence = 0.9F;
    msg.targets.push_back(tgt);
  }
  msg.confidence = 1.0f;
  return msg;
}

auto make_msg_with_sog(const std::vector<uint64_t>& ids, double sog) {
  auto msg = make_msg(ids);
  for (auto& tgt : msg.targets) {
    tgt.sog_kn = sog;
  }
  return msg;
}

// Default configuration (generous eviction params for predictable tests)
auto default_cfg() {
  return TrackBuffer::Config{256, 10, 100.0};
}

// ──────────────────────────────────────────────
// Test 1: Insert single target and retrieve
// ──────────────────────────────────────────────
TEST(TrackBufferTest, InsertSingleTarget) {
  TrackBuffer buf(default_cfg());

  auto t0 = time_point{};
  auto msg = make_msg({1001});
  buf.update(msg, t0);

  auto snap = buf.get(1001);
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(snap->target_id, 1001);
  EXPECT_DOUBLE_EQ(snap->sog_kn, 10.0);
  EXPECT_DOUBLE_EQ(snap->latitude_deg, 35.0);
  EXPECT_DOUBLE_EQ(snap->longitude_deg, 139.0);
  EXPECT_EQ(buf.size(), 1);
}

// ──────────────────────────────────────────────
// Test 2: Upsert updates existing target
// ──────────────────────────────────────────────
TEST(TrackBufferTest, UpsertUpdatesExisting) {
  TrackBuffer buf(default_cfg());

  auto t0 = time_point{};

  buf.update(make_msg_with_sog({1001}, 10.0), t0);
  // Same target_id with updated sog
  buf.update(make_msg_with_sog({1001}, 15.0), t0 + std::chrono::seconds(1));

  auto snap = buf.get(1001);
  ASSERT_TRUE(snap.has_value());
  EXPECT_DOUBLE_EQ(snap->sog_kn, 15.0);
  EXPECT_EQ(buf.size(), 1);
}

// ──────────────────────────────────────────────
// Test 3: Max capacity eviction
// ──────────────────────────────────────────────
TEST(TrackBufferTest, MaxCapacityEviction) {
  TrackBuffer buf({3, 10, 100.0});  // capacity=3

  auto t0 = time_point{};
  buf.update(make_msg({1, 2, 3}), t0);
  EXPECT_EQ(buf.size(), 3);

  // Insert a 4th → should evict oldest (1)
  buf.update(make_msg({4}), t0 + std::chrono::seconds(1));
  EXPECT_EQ(buf.size(), 3);

  EXPECT_FALSE(buf.get(1).has_value());  // evicted
  EXPECT_TRUE(buf.get(4).has_value());   // inserted
  EXPECT_TRUE(buf.get(2).has_value());   // still present
  EXPECT_TRUE(buf.get(3).has_value());   // still present
}

// ──────────────────────────────────────────────
// Test 4: Stale targets evicted by miss_count
// ──────────────────────────────────────────────
TEST(TrackBufferTest, StaleTargetsEvicted) {
  TrackBuffer buf({256, 3, 100.0});  // disappearance_periods=3

  auto t0 = time_point{};
  buf.update(make_msg({1001}), t0);
  EXPECT_EQ(buf.size(), 1);

  // Each evict_stale call increments miss_count for ALL entries.
  // After 3 calls, miss_count=3 >= 3 → evicted.
  buf.evict_stale(t0 + std::chrono::seconds(1));
  EXPECT_EQ(buf.size(), 1);  // still alive
  buf.evict_stale(t0 + std::chrono::seconds(2));
  EXPECT_EQ(buf.size(), 1);  // still alive
  buf.evict_stale(t0 + std::chrono::seconds(3));
  EXPECT_EQ(buf.size(), 0);  // evicted!

  EXPECT_FALSE(buf.get(1001).has_value());
}

// ──────────────────────────────────────────────
// Test 5: Active targets filtering
// ──────────────────────────────────────────────
TEST(TrackBufferTest, ActiveTargetsFiltered) {
  TrackBuffer buf({256, 3, 100.0});  // miss threshold = 3

  auto t0 = time_point{};
  buf.update(make_msg({1, 2}), t0);
  buf.evict_stale(t0 + std::chrono::seconds(1));  // both miss=1
  EXPECT_EQ(buf.active_targets().size(), 2);

  buf.evict_stale(t0 + std::chrono::seconds(2));  // both miss=2 (2<3)
  EXPECT_EQ(buf.active_targets().size(), 2);

  buf.evict_stale(t0 + std::chrono::seconds(3));  // both miss=3 >= 3
  // Both are still in the buffer (not yet evicted) but filtered from active.
  // Actually evict_stale removes them when miss_count >= 3.
  EXPECT_EQ(buf.active_targets().size(), 0);
  EXPECT_EQ(buf.size(), 0);
}

// ──────────────────────────────────────────────
// Test 6: Get returns nullopt for missing target
// ──────────────────────────────────────────────
TEST(TrackBufferTest, GetReturnsNulloptForMissing) {
  TrackBuffer buf(default_cfg());

  auto t0 = time_point{};
  buf.update(make_msg({42}), t0);

  EXPECT_TRUE(buf.get(42).has_value());
  EXPECT_FALSE(buf.get(99).has_value());
  EXPECT_FALSE(buf.get(0).has_value());
}

// ──────────────────────────────────────────────
// Test 7: Thread safety — concurrent access
// ──────────────────────────────────────────────
TEST(TrackBufferTest, ThreadSafety) {
  // Large capacity so no eviction during test.
  TrackBuffer buf({10000, 10000, 10000.0});
  constexpr int kIterations = 5000;
  std::atomic<int> writes_done{0};

  // Writer: continuously insert new targets and periodically evict stale.
  std::thread writer([&]() {
    for (int i = 1; i <= kIterations; ++i) {
      auto msg = make_msg({static_cast<uint64_t>(i)});
      buf.update(msg, std::chrono::steady_clock::now());
      if (i % 50 == 0) {
        buf.evict_stale(std::chrono::steady_clock::now());
      }
      writes_done++;
    }
  });

  // Reader: concurrently query buffer state.
  std::thread reader([&]() {
    int reads = 0;
    while (writes_done < kIterations) {
      (void)buf.size();
      (void)buf.active_targets();
      if (reads % 5 == 0) {
        (void)buf.get(static_cast<uint64_t>(reads));
      }
      reads++;
    }
  });

  writer.join();
  reader.join();

  // After all writes complete, buffer should be in a valid, queryable state.
  EXPECT_NO_THROW(static_cast<void>(buf.size()));
  EXPECT_NO_THROW(static_cast<void>(buf.active_targets()));
  EXPECT_GT(buf.size(), 0U);
}

// ──────────────────────────────────────────────
// Test 8: Multiple targets inserted and retrieved
// ──────────────────────────────────────────────
TEST(TrackBufferTest, MultipleTargets) {
  TrackBuffer buf(default_cfg());

  auto t0 = time_point{};
  buf.update(make_msg({10, 20, 30, 40, 50}), t0);
  EXPECT_EQ(buf.size(), 5);

  for (const uint64_t id : {uint64_t{10}, uint64_t{20}, uint64_t{30}, uint64_t{40}, uint64_t{50}}) {
    auto snap = buf.get(id);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->target_id, id);
  }

  auto active = buf.active_targets();
  EXPECT_EQ(active.size(), 5);
}

// ──────────────────────────────────────────────
// Test 9: Empty array does not crash or grow buffer
// ──────────────────────────────────────────────
TEST(TrackBufferTest, UpdateWithEmptyArray) {
  TrackBuffer buf(default_cfg());

  l3_external_msgs::msg::TrackedTargetArray empty_msg;
  empty_msg.confidence = 1.0f;

  auto t0 = time_point{};

  EXPECT_NO_THROW(buf.update(empty_msg, t0));
  EXPECT_EQ(buf.size(), 0);

  // Populate, then update with empty array — existing targets remain.
  buf.update(make_msg({1, 2, 3}), t0);
  EXPECT_EQ(buf.size(), 3);

  EXPECT_NO_THROW(buf.update(empty_msg, t0 + std::chrono::seconds(1)));
  EXPECT_EQ(buf.size(), 3);  // empty update does not remove anything
}

}  // namespace
}  // namespace mass_l3::m2
