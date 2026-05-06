#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

#include "m6_colregs_reasoner/rule_library_loader.hpp"

namespace mass_l3::m6_colregs {

class RuleLibraryLoaderTest : public ::testing::Test {
 protected:
  std::string CreateYamlFile(const std::string& content) {
    char tmp_path[] = "/tmp/rule_lib_test_XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd != -1) {
      std::ofstream ofs(tmp_path);
      ofs << content;
      ofs.close();
      ::close(fd);
    }
    tmp_files_.push_back(tmp_path);
    return tmp_path;
  }

  void TearDown() override {
    for (const auto& f : tmp_files_) {
      std::remove(f.c_str());
    }
  }

 private:
  std::vector<std::string> tmp_files_;
};

TEST_F(RuleLibraryLoaderTest, LoadsAllColregsRules) {
  std::string yaml = R"(
colregs_rules:
  - rule_id: 5
    enabled: true
  - rule_id: 6
    enabled: true
  - rule_id: 7
    enabled: true
  - rule_id: 8
    enabled: true
  - rule_id: 13
    enabled: true
  - rule_id: 14
    enabled: true
  - rule_id: 15
    enabled: true
  - rule_id: 16
    enabled: true
  - rule_id: 17
    enabled: true
  - rule_id: 18
    enabled: true
  - rule_id: 19
    enabled: true
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto rules = loader.load_colregs_rules();
  EXPECT_EQ(rules.size(), 11u);
}

TEST_F(RuleLibraryLoaderTest, LoadsSubsetOfRules) {
  std::string yaml = R"(
colregs_rules:
  - rule_id: 14
    enabled: true
  - rule_id: 15
    enabled: true
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto rules = loader.load_colregs_rules();
  ASSERT_EQ(rules.size(), 2u);
  EXPECT_EQ(rules[0]->rule_id(), 14);
  EXPECT_EQ(rules[1]->rule_id(), 15);
}

TEST_F(RuleLibraryLoaderTest, DisabledRulesNotLoaded) {
  std::string yaml = R"(
colregs_rules:
  - rule_id: 5
    enabled: false
  - rule_id: 14
    enabled: true
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto rules = loader.load_colregs_rules();
  ASSERT_EQ(rules.size(), 1u);
  EXPECT_EQ(rules[0]->rule_id(), 14);
}

TEST_F(RuleLibraryLoaderTest, EmptyConfigReturnsEmpty) {
  std::string yaml = R"(
colregs_rules:
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto rules = loader.load_colregs_rules();
  EXPECT_TRUE(rules.empty());
}

TEST_F(RuleLibraryLoaderTest, UnknownRuleIdSilentlySkipped) {
  std::string yaml = R"(
colregs_rules:
  - rule_id: 99
    enabled: true
  - rule_id: 14
    enabled: true
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto rules = loader.load_colregs_rules();
  ASSERT_EQ(rules.size(), 1u);
  EXPECT_EQ(rules[0]->rule_id(), 14);
}

TEST_F(RuleLibraryLoaderTest, CeVniRulesStubReturnsEmpty) {
  std::string yaml = R"(
colregs_rules:
  - rule_id: 14
    enabled: true
)";
  auto path = CreateYamlFile(yaml);
  RuleLibraryLoader loader(path);
  auto cevni_rules = loader.load_cevni_rules();
  EXPECT_TRUE(cevni_rules.empty());
}

}  // namespace mass_l3::m6_colregs
