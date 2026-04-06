"""Repository rule that downloads and builds pugixml from source.

Downloads the official pugixml release tarball from GitHub and builds it
as a Bazel cc_library target.  This approach works on Linux, macOS and
Windows without requiring any platform-specific system package.
"""

_VERSION = "1.14"
_URL = "https://github.com/zeux/pugixml/releases/download/v{v}/pugixml-{v}.tar.gz".format(v = _VERSION)
_SHA256 = "2f10e276870c64b1db6809050a75e11a897a8d7456c4be5c6b2e35a11168a015"

def _pugixml_impl(rctx):
    rctx.download_and_extract(
        url = _URL,
        sha256 = _SHA256,
        stripPrefix = "pugixml-{v}".format(v = _VERSION),
    )
    rctx.file("BUILD.bazel", content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "pugixml",
    srcs = ["src/pugixml.cpp"],
    hdrs = ["src/pugixml.hpp", "src/pugiconfig.hpp"],
    includes = ["src"],
    visibility = ["//visibility:public"],
)
""")

pugixml_configure = repository_rule(
    implementation = _pugixml_impl,
    local = False,
)
