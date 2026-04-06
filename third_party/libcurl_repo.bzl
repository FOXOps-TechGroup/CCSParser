"""Repository rule that wraps the system-installed libcurl.

Platform support:
  Linux   – pkg-config, then known apt/multiarch paths.
  macOS   – pkg-config + xcrun SDK fallback, then Homebrew paths.
  Windows – probes vcpkg (x64/x86/arm64), MSYS2/MinGW64, and common paths.
            Generates a cc_import for the import library (.lib) plus the
            required Windows system libraries (Ws2_32, Wldap32, Crypt32,
            Normaliz).
"""

# ── Unix candidate paths ──────────────────────────────────────────────────────

_UNIX_FALLBACK_PATHS = [
    # macOS – Homebrew Apple Silicon
    "/opt/homebrew/opt/curl/include",
    "/opt/homebrew/include",
    # macOS – Homebrew Intel
    "/usr/local/opt/curl/include",
    "/usr/local/include",
    # Linux – multiarch (Debian/Ubuntu x86-64)
    "/usr/include/x86_64-linux-gnu",
    # Linux – generic
    "/usr/include",
]

# ── Windows candidate include paths ──────────────────────────────────────────
# Probed in priority order; the first directory containing curl\curl.h wins.

_WIN_INCLUDE_CANDIDATES = [
    # vcpkg default triplets
    "C:/vcpkg/installed/x64-windows/include",
    "C:/vcpkg/installed/x86-windows/include",
    "C:/vcpkg/installed/arm64-windows/include",
    # MSYS2 / MinGW64 (also bundled with Git for Windows)
    "C:/msys64/mingw64/include",
    "C:/msys64/usr/include",
    "C:/msys32/mingw32/include",
    # Chocolatey
    "C:/ProgramData/chocolatey/lib/curl/tools/include",
    # Manual installations
    "C:/curl/include",
    "C:/Program Files/curl/include",
    "C:/Program Files (x86)/curl/include",
]

# Windows system libraries that libcurl depends on (MSVC / clang-cl format).
_WIN_SYSTEM_LINKOPTS = [
    "-DEFAULTLIB:Ws2_32.lib",
    "-DEFAULTLIB:Wldap32.lib",
    "-DEFAULTLIB:Crypt32.lib",
    "-DEFAULTLIB:Normaliz.lib",
]

# ── Helpers ───────────────────────────────────────────────────────────────────

def _probe_unix(rctx, path):
    """Return True if curl/curl.h exists under *path* (Unix sh)."""
    r = rctx.execute(["sh", "-c", "test -f '{p}/curl/curl.h' && echo found".format(p = path)])
    return r.return_code == 0 and "found" in r.stdout

_CMD = "C:\\Windows\\System32\\cmd.exe"

def _probe_win_dir(rctx, path):
    """Return True if curl\\curl.h exists under *path* (Windows cmd)."""
    win_path = path.replace("/", "\\")
    r = rctx.execute([_CMD, "/c", "if exist \"{p}\\curl\\curl.h\" echo found".format(p = win_path)])
    return r.return_code == 0 and "found" in r.stdout

def _probe_win_file(rctx, path):
    """Return True if the single file *path* exists (Windows cmd)."""
    win_path = path.replace("/", "\\")
    r = rctx.execute([_CMD, "/c", "if exist \"{p}\" echo found".format(p = win_path)])
    return r.return_code == 0 and "found" in r.stdout

# ── Unix implementation ───────────────────────────────────────────────────────

def _libcurl_unix(rctx):
    include_path = None
    linkopts = ["-lcurl"]

    # Step 1: pkg-config (works on Linux and macOS with Homebrew curl).
    pc = rctx.execute(["pkg-config", "--cflags", "--libs", "libcurl"])
    if pc.return_code == 0 and pc.stdout.strip():
        for flag in pc.stdout.strip().split(" "):
            if flag.startswith("-I") and include_path == None:
                include_path = flag[2:]
            elif flag.startswith("-L"):
                linkopts = [flag, "-lcurl"]

    # Step 2: macOS Xcode SDK — pkg-config exits 0 but emits no -I flag.
    if include_path == None:
        sdk = rctx.execute(["xcrun", "--show-sdk-path"])
        if sdk.return_code == 0:
            candidate = sdk.stdout.strip() + "/usr/include"
            if _probe_unix(rctx, candidate):
                include_path = candidate

    # Step 3: Homebrew / Linux known paths.
    if include_path == None:
        for candidate in _UNIX_FALLBACK_PATHS:
            if _probe_unix(rctx, candidate):
                if "/homebrew/" in candidate or "/local/opt/" in candidate:
                    lib_dir = candidate.replace("/include", "/lib")
                    linkopts = ["-L" + lib_dir, "-lcurl"]
                include_path = candidate
                break

    if include_path == None:
        fail(
            "libcurl headers not found.\n" +
            "  Linux:  apt-get install libcurl4-openssl-dev\n" +
            "  macOS:  xcode-select --install  (or: brew install curl)",
        )

    rctx.symlink(include_path + "/curl", "curl")
    rctx.file("BUILD.bazel", content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "curl",
    hdrs = glob(["curl/*.h"]),
    includes = ["."],
    linkopts = {linkopts},
    visibility = ["//visibility:public"],
)
""".format(linkopts = repr(linkopts)))

# ── Windows implementation ────────────────────────────────────────────────────

def _libcurl_windows(rctx):
    # Locate headers.
    # Prepend paths derived from VCPKG_INSTALLATION_ROOT so CI environments
    # (e.g. GitHub Actions, where the variable is set by the runner image) are
    # found before the hardcoded fallback list.
    candidates = list(_WIN_INCLUDE_CANDIDATES)
    vcpkg_root = rctx.os.environ.get("VCPKG_INSTALLATION_ROOT", "")
    if vcpkg_root:
        vcpkg_root = vcpkg_root.replace("\\", "/").rstrip("/")
        candidates = [
            vcpkg_root + "/installed/x64-windows/include",
            vcpkg_root + "/installed/x86-windows/include",
            vcpkg_root + "/installed/arm64-windows/include",
        ] + candidates

    include_path = None
    for candidate in candidates:
        if _probe_win_dir(rctx, candidate):
            include_path = candidate
            break

    if include_path == None:
        # Collect diagnostics to help identify whether the probe command itself
        # is failing or the files are genuinely absent.
        diag_lines = ["libcurl headers not found on Windows. Probe diagnostics:"]
        for candidate in candidates:
            win_path = candidate.replace("/", "\\")
            r = rctx.execute([_CMD, "/c", "if exist \"{p}\\curl\\curl.h\" (echo FOUND) else (echo MISSING)".format(p = win_path)])
            diag_lines.append("  [{rc}] {p} -> stdout={out!r} stderr={err!r}".format(
                rc = r.return_code,
                p = candidate,
                out = r.stdout.strip(),
                err = r.stderr.strip(),
            ))
        # Also check whether cmd.exe itself is reachable.
        r_ver = rctx.execute([_CMD, "/c", "ver"])
        diag_lines.append("  cmd ver: rc={} out={!r}".format(r_ver.return_code, r_ver.stdout.strip()))
        fail("\n".join(diag_lines))

    # Copy headers into the external repo (symlinks require admin/DeveloperMode).
    win_src = include_path.replace("/", "\\") + "\\curl"
    rctx.execute([_CMD, "/c", "xcopy /E /I /Y \"{src}\" curl\\".format(src = win_src)])

    # Locate the import library (.lib or MinGW .dll.a).
    lib_dir = include_path.replace("/include", "/lib")
    lib_candidates = [
        lib_dir + "/libcurl.lib",
        lib_dir + "/libcurl_imp.lib",
        lib_dir + "/curl.lib",
        lib_dir + "/libcurl.dll.a",
    ]
    found_lib = None
    for lib in lib_candidates:
        if _probe_win_file(rctx, lib):
            found_lib = lib
            break

    # Locate the DLL for cc_import shared_library field.
    bin_dir = include_path.replace("/include", "/bin")
    dll_candidates = [
        bin_dir + "/libcurl.dll",
        bin_dir + "/curl.dll",
        lib_dir + "/libcurl.dll",
    ]
    found_dll = None
    for dll in dll_candidates:
        if _probe_win_file(rctx, dll):
            found_dll = dll
            rctx.symlink(dll, "libcurl.dll")
            break

    if found_lib != None:
        # MSVC / clang-cl path: use cc_import with the import library.
        rctx.symlink(found_lib, "libcurl.lib")
        if found_dll != None:
            import_block = """\
cc_import(
    name = "curl_import",
    interface_library = "libcurl.lib",
    shared_library = "libcurl.dll",
)
"""
        else:
            import_block = """\
cc_import(
    name = "curl_import",
    static_library = "libcurl.lib",
)
"""
        build_content = """\
load("@rules_cc//cc:defs.bzl", "cc_import", "cc_library")

{import_block}
cc_library(
    name = "curl",
    hdrs = glob(["curl/*.h"]),
    includes = ["."],
    deps = [":curl_import"],
    linkopts = {sys_linkopts},
    visibility = ["//visibility:public"],
)
""".format(import_block = import_block, sys_linkopts = repr(_WIN_SYSTEM_LINKOPTS))
    else:
        # MinGW without .lib: rely on -lcurl linker flag (GCC toolchain).
        build_content = """\
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "curl",
    hdrs = glob(["curl/*.h"]),
    includes = ["."],
    linkopts = ["-lcurl"],
    visibility = ["//visibility:public"],
)
"""

    rctx.file("BUILD.bazel", content = build_content)

# ── Entry point ───────────────────────────────────────────────────────────────

def _libcurl_impl(rctx):
    if rctx.os.name.startswith("windows"):
        _libcurl_windows(rctx)
    else:
        _libcurl_unix(rctx)

libcurl_configure = repository_rule(
    implementation = _libcurl_impl,
    local = True,  # System-dependent.
    environ = ["VCPKG_INSTALLATION_ROOT"],
)
