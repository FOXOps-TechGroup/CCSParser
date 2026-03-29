"""Module extension for third-party dependencies."""

load("//third_party:nlohmann_json_repo.bzl", "nlohmann_json_configure")
load("//third_party:pugixml_repo.bzl", "pugixml_configure")
load("//third_party:libcurl_repo.bzl", "libcurl_configure")

def _third_party_deps_impl(mctx):
    nlohmann_json_configure(name = "nlohmann_json")
    pugixml_configure(name = "pugixml")
    libcurl_configure(name = "libcurl")
    return mctx.extension_metadata(reproducible = False)

third_party_deps = module_extension(
    implementation = _third_party_deps_impl,
)
