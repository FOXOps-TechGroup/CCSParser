// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Integration tests for contest package parsing and file-ref resolution.
// Uses temporary directories to simulate package structures.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "ccsparser/ccsparser.h"
#include "ccsparser/file_ref_resolver.h"

namespace ccsparser {
namespace {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test fixture with temp directory management.
// ---------------------------------------------------------------------------

class PackageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    package_root_ =
        fs::temp_directory_path() /
        ("ccsparser_pkg_test_" +
         std::to_string(std::hash<std::string>{}(
             ::testing::UnitTest::GetInstance()
                 ->current_test_info()
                 ->name())));
    fs::create_directories(package_root_);
  }

  void TearDown() override { fs::remove_all(package_root_); }

  void WriteFile(const fs::path& relative_path,
                 const std::string& content) {
    auto full = package_root_ / relative_path;
    fs::create_directories(full.parent_path());
    std::ofstream ofs(full);
    ASSERT_TRUE(ofs.is_open()) << "Cannot write: " << full;
    ofs << content;
  }

  // Write a minimal 2023-06 event-feed.ndjson.
  void WriteMinimalEventFeed() {
    std::string feed;
    feed += R"({"type":"state","data":{},"token":"t0"})" "\n";
    feed +=
        R"({"type":"contest","id":"pkg","data":{"id":"pkg","name":"Package Test","penalty_time":20},"token":"t1"})"
        "\n";
    feed +=
        R"({"type":"judgement-types","id":"AC","data":{"id":"AC","name":"correct","penalty":false,"solved":true},"token":"t2"})"
        "\n";
    feed +=
        R"({"type":"problems","id":"A","data":{"id":"A","ordinal":1,"label":"A","name":"Apples"},"token":"t3"})"
        "\n";
    feed +=
        R"({"type":"teams","id":"t1","data":{"id":"t1","name":"Alpha","photo":[{"href":"teams/t1/photo.jpg","mime":"image/jpeg"}]},"token":"t4"})"
        "\n";
    feed +=
        R"({"type":"state","data":{"started":"2025-01-01T10:00:00Z","end_of_updates":"2025-01-01T15:00:00Z"},"token":"t5"})"
        "\n";
    WriteFile("event-feed.ndjson", feed);
  }

  fs::path package_root_;
};

// =====================================================================
// package_dir_with_event_feed
// =====================================================================

TEST_F(PackageTest, package_dir_with_event_feed) {
  WriteMinimalEventFeed();

  ParseOptions opts;
  opts.version = ApiVersion::kAuto;
  opts.error_policy = ErrorPolicy::kContinue;

  auto result = PackageParser::ParsePackageDirectory(package_root_, opts);
  ASSERT_TRUE(result.ok()) << result.status().ToString();

  const auto& pr = result.value();
  ASSERT_NE(pr.store.GetContest(), nullptr);
  EXPECT_EQ(pr.store.GetContest()->name.value_or(""), "Package Test");
  EXPECT_EQ(pr.store.ListObjects(ObjectType::kProblems).size(), 1u);
  EXPECT_EQ(pr.store.ListObjects(ObjectType::kTeams).size(), 1u);
  EXPECT_TRUE(pr.cursor.end_of_updates);
}

// =====================================================================
// file_ref_resolution
// =====================================================================

TEST_F(PackageTest, file_ref_resolution) {
  WriteMinimalEventFeed();
  // Create the actual photo file that the team references.
  WriteFile("teams/t1/photo.jpg", "fake-jpeg-content");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "teams/t1/photo.jpg";
  ref.mime = "image/jpeg";

  auto resolved = resolver.ResolveToLocalPath(ref, "teams", "t1");
  ASSERT_TRUE(resolved.ok()) << resolved.status().ToString();
  EXPECT_TRUE(fs::exists(resolved.value()));

  // Also verify via Exists helper.
  EXPECT_TRUE(resolver.Exists(ref, "teams", "t1"));
}

// =====================================================================
// missing_resource_diagnostic
// =====================================================================

TEST_F(PackageTest, missing_resource_diagnostic) {
  // The resolver should report an error for a missing file.
  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "teams/t99/photo.jpg";

  auto resolved = resolver.ResolveToLocalPath(ref, "teams", "t99");
  EXPECT_FALSE(resolved.ok());
  EXPECT_EQ(resolved.status().code(), StatusCode::kNotFound);

  EXPECT_FALSE(resolver.Exists(ref, "teams", "t99"));
}

// =====================================================================
// package_with_nested_file_refs
// =====================================================================

TEST_F(PackageTest, package_with_nested_file_refs) {
  WriteMinimalEventFeed();
  WriteFile("problems/A/statement.pdf", "fake-pdf");
  WriteFile("teams/t1/photo.jpg", "fake-jpeg");

  FileRefResolver resolver(package_root_);

  // Problem statement.
  FileRef stmt_ref;
  stmt_ref.href = "problems/A/statement.pdf";
  auto stmt = resolver.ResolveToLocalPath(stmt_ref, "problems", "A");
  ASSERT_TRUE(stmt.ok());
  EXPECT_TRUE(fs::exists(stmt.value()));

  // Team photo.
  FileRef photo_ref;
  photo_ref.href = "teams/t1/photo.jpg";
  auto photo = resolver.ResolveToLocalPath(photo_ref, "teams", "t1");
  ASSERT_TRUE(photo.ok());
  EXPECT_TRUE(fs::exists(photo.value()));
}

// =====================================================================
// package_missing_event_feed
// =====================================================================

TEST_F(PackageTest, package_missing_event_feed) {
  // Empty directory — no event-feed.ndjson.
  ParseOptions opts;
  opts.version = ApiVersion::kAuto;

  auto result = PackageParser::ParsePackageDirectory(package_root_, opts);
  // Should fail because event-feed.ndjson is missing.
  EXPECT_FALSE(result.ok());
}

}  // namespace
}  // namespace ccsparser
