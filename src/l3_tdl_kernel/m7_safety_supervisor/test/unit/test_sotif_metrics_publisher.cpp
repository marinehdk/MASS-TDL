#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <cstdint>

#include "m7_safety_supervisor/sotif/sotif_metrics_publisher.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "l3_msgs/msg/sotif_metrics.hpp"

using namespace mass_l3::m7::sotif;
using namespace std::chrono_literals;

namespace {

class SotifMetricsPublisherTest : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<rclcpp::Node>("test_sotif_metrics_publisher");
    publisher_ = std::make_unique<SotifMetricsPublisher>(node_.get());
  }

  void TearDown() override {
    publisher_.reset();
    node_.reset();
    rclcpp::shutdown();
  }

  static AssumptionStatus make_status(bool all_violated, float fill_value) {
    AssumptionStatus s{};
    for (std::size_t i = 0; i < static_cast<std::size_t>(AssumptionId::kCount); ++i) {
      s.violation_active[i] = all_violated;
      s.violation_metric[i] = fill_value;
    }
    return s;
  }

  std::shared_ptr<rclcpp::Node> node_;
  std::unique_ptr<SotifMetricsPublisher> publisher_;
};

TEST_F(SotifMetricsPublisherTest, PublishDefaultModeIsStub) {
  auto status = make_status(true, 0.85F);
  ASSERT_NO_THROW(publisher_->publish(status, 42));
}

TEST_F(SotifMetricsPublisherTest, PublishStubModeAllZeros) {
  auto status = make_status(true, 0.85F);
  publisher_->set_stub_mode(true);
  ASSERT_NO_THROW(publisher_->publish(status, 99));
}

TEST_F(SotifMetricsPublisherTest, PublishRealModeKeepsViolationScore) {
  publisher_->set_stub_mode(false);
  auto status = make_status(false, 0.5F);
  ASSERT_NO_THROW(publisher_->publish(status, 10));
}

TEST_F(SotifMetricsPublisherTest, PublishWithEmptyStatus) {
  AssumptionStatus s{};
  ASSERT_NO_THROW(publisher_->publish(s, 0));
}

TEST_F(SotifMetricsPublisherTest, ToggleStubMode) {
  publisher_->set_stub_mode(false);
  publisher_->set_stub_mode(true);
  publisher_->set_stub_mode(false);
  ASSERT_NO_THROW(publisher_->publish(make_status(true, 0.9F), 50));
}

}  // namespace
