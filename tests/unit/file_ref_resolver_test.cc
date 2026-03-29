// Copyright 2025 CCSParser FOXOps
//
// Licensed under the MIT License. See LICENSE for details.

// Tests for FileRefResolver: resolving file references to local paths
// within a contest package directory.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "ccsparser/file_ref_resolver.h"
#include "ccsparser/status.h"
#include "ccsparser/types.h"

namespace ccsparser {
namespace {

namespace fs = std::filesystem;

// Test fixture that creates a temporary package directory.
class FileRefResolverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a unique temporary directory.
    package_root_ =
        fs::temp_directory_path() / ("ccsparser_test_" + std::to_string(
                                         std::hash<std::string>{}(
                                             ::testing::UnitTest::GetInstance()
                                                 ->current_test_info()
                                                 ->name())));
    fs::create_directories(package_root_);
  }

  void TearDown() override {
    fs::remove_all(package_root_);
  }

  // Create a file with optional content.
  void CreateFile(const fs::path& relative_path,
                  const std::string& content = "test") {
    auto full_path = package_root_ / relative_path;
    fs::create_directories(full_path.parent_path());
    std::ofstream(full_path) << content;
  }

  fs::path package_root_;
};

// =====================================================================
// Direct href resolution
// =====================================================================

TEST_F(FileRefResolverTest, ResolveDirectHref) {
  CreateFile("teams/t1/photo.jpg");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "teams/t1/photo.jpg";

  auto result = resolver.ResolveToLocalPath(ref, "teams", "t1");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_TRUE(fs::exists(result.value()));
  EXPECT_EQ(result.value(), package_root_ / "teams/t1/photo.jpg");
}

// =====================================================================
// Pattern-based resolution (endpoint/id/filename)
// =====================================================================

TEST_F(FileRefResolverTest, ResolveByFilenamePattern) {
  CreateFile("problems/A/statement.pdf");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "https://example.com/api/problems/A/statement";
  ref.filename = "statement.pdf";

  auto result = resolver.ResolveToLocalPath(ref, "problems", "A");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_TRUE(fs::exists(result.value()));
}

// =====================================================================
// Href filename extraction fallback
// =====================================================================

TEST_F(FileRefResolverTest, ResolveByHrefFilenameExtraction) {
  CreateFile("organizations/org1/logo.png");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "https://example.com/api/organizations/org1/logo.png";

  auto result = resolver.ResolveToLocalPath(ref, "organizations", "org1");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
  EXPECT_TRUE(fs::exists(result.value()));
}

// =====================================================================
// File not found
// =====================================================================

TEST_F(FileRefResolverTest, FileNotFound) {
  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "nonexistent/file.txt";

  auto result = resolver.ResolveToLocalPath(ref, "problems", "A");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), StatusCode::kNotFound);
}

// =====================================================================
// Exists helper
// =====================================================================

TEST_F(FileRefResolverTest, ExistsReturnsTrueForExistingFile) {
  CreateFile("teams/t1/photo.jpg");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "teams/t1/photo.jpg";

  EXPECT_TRUE(resolver.Exists(ref, "teams", "t1"));
}

TEST_F(FileRefResolverTest, ExistsReturnsFalseForMissingFile) {
  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "missing.txt";

  EXPECT_FALSE(resolver.Exists(ref, "teams", "t1"));
}

// =====================================================================
// Multiple resolution strategies
// =====================================================================

TEST_F(FileRefResolverTest, DirectHrefPreferredOverPattern) {
  // Create file at both direct and pattern locations.
  CreateFile("direct/photo.jpg", "direct content");
  CreateFile("teams/t1/photo.jpg", "pattern content");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "direct/photo.jpg";
  ref.filename = "photo.jpg";

  auto result = resolver.ResolveToLocalPath(ref, "teams", "t1");
  ASSERT_TRUE(result.ok());
  // Direct href should be preferred.
  EXPECT_EQ(result.value(), package_root_ / "direct/photo.jpg");
}

// =====================================================================
// Empty href
// =====================================================================

TEST_F(FileRefResolverTest, EmptyHrefWithFilename) {
  CreateFile("teams/t1/photo.jpg");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "";
  ref.filename = "photo.jpg";

  auto result = resolver.ResolveToLocalPath(ref, "teams", "t1");
  ASSERT_TRUE(result.ok()) << result.status().ToString();
}

TEST_F(FileRefResolverTest, EmptyHrefNoFilename) {
  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "";

  auto result = resolver.ResolveToLocalPath(ref, "teams", "t1");
  EXPECT_FALSE(result.ok());
}

// =====================================================================
// FileRef struct fields
// =====================================================================

TEST(FileRefStructTest, DefaultConstruction) {
  FileRef ref;
  EXPECT_TRUE(ref.href.empty());
  EXPECT_FALSE(ref.filename.has_value());
  EXPECT_FALSE(ref.mime.has_value());
  EXPECT_FALSE(ref.width.has_value());
  EXPECT_FALSE(ref.height.has_value());
  EXPECT_TRUE(ref.tags.empty());
  EXPECT_TRUE(ref.unknown_fields.empty());
}

TEST(FileRefStructTest, FieldAssignment) {
  FileRef ref;
  ref.href = "test.png";
  ref.filename = "test.png";
  ref.mime = "image/png";
  ref.width = 100;
  ref.height = 200;
  ref.tags = {"thumbnail", "logo"};

  EXPECT_EQ(ref.href, "test.png");
  EXPECT_EQ(ref.width.value(), 100);
  EXPECT_EQ(ref.height.value(), 200);
  EXPECT_EQ(ref.tags.size(), 2);
}

// =====================================================================
// Nested directory structures
// =====================================================================

TEST_F(FileRefResolverTest, DeeplyNestedPath) {
  CreateFile("teams/t1/media/photos/official.jpg");

  FileRefResolver resolver(package_root_);
  FileRef ref;
  ref.href = "teams/t1/media/photos/official.jpg";

  auto result = resolver.ResolveToLocalPath(ref, "teams", "t1");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(fs::exists(result.value()));
}

}  // namespace
}  // namespace ccsparser
