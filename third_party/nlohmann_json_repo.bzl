"""Repository rule that downloads the nlohmann/json single-header library."""

_VERSION = "3.11.3"
_URL = "https://github.com/nlohmann/json/releases/download/v{v}/json.hpp".format(v = _VERSION)
_SHA256 = "9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6"

def _nlohmann_json_impl(rctx):
    rctx.download(
        url = _URL,
        output = "nlohmann/json.hpp",
        sha256 = _SHA256,
    )
    rctx.file("BUILD.bazel", content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "json",
    hdrs = ["nlohmann/json.hpp"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
""")

nlohmann_json_configure = repository_rule(
    implementation = _nlohmann_json_impl,
    local = False,
)
