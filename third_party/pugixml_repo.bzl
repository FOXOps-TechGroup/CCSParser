"""Repository rule that wraps the system-installed pugixml."""

def _pugixml_impl(rctx):
    # Find system pugixml headers.
    result = rctx.execute(["sh", "-c", "test -f /usr/include/pugixml.hpp && echo found"])
    if result.return_code != 0 or "found" not in result.stdout:
        fail("libpugixml-dev not found. Install with: apt-get install libpugixml-dev")

    rctx.symlink("/usr/include/pugixml.hpp", "pugixml.hpp")
    rctx.symlink("/usr/include/pugiconfig.hpp", "pugiconfig.hpp")
    rctx.file("BUILD.bazel", content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "pugixml",
    hdrs = ["pugixml.hpp", "pugiconfig.hpp"],
    includes = ["."],
    linkopts = ["-lpugixml"],
    visibility = ["//visibility:public"],
)
""")

pugixml_configure = repository_rule(
    implementation = _pugixml_impl,
    local = True,
)
