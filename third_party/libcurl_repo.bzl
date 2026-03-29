"""Repository rule that wraps the system-installed libcurl."""

def _libcurl_impl(rctx):
    # Locate the system libcurl headers.
    # On Debian/Ubuntu with multiarch, headers are under /usr/include/<arch>/
    result = rctx.execute(["pkg-config", "--cflags", "libcurl"])
    if result.return_code != 0:
        # Fallback: try common paths.
        result2 = rctx.execute(["sh", "-c", "test -f /usr/include/x86_64-linux-gnu/curl/curl.h && echo found"])
        if result2.return_code == 0 and "found" in result2.stdout:
            include_path = "/usr/include/x86_64-linux-gnu"
        elif rctx.execute(["sh", "-c", "test -f /usr/include/curl/curl.h"]).return_code == 0:
            include_path = "/usr/include"
        else:
            fail("libcurl-dev not found. Install with: apt-get install libcurl4-openssl-dev")
    else:
        # Parse -I flag from pkg-config output.
        cflags = result.stdout.strip()
        include_path = "/usr/include"
        for flag in cflags.split(" "):
            if flag.startswith("-I"):
                include_path = flag[2:]
                break

    rctx.symlink(include_path + "/curl", "curl")
    rctx.file("BUILD.bazel", content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "curl",
    hdrs = glob(["curl/*.h"]),
    includes = ["."],
    linkopts = ["-lcurl"],
    visibility = ["//visibility:public"],
)
""")

libcurl_configure = repository_rule(
    implementation = _libcurl_impl,
    local = True,  # System-dependent.
)
