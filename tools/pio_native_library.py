"""Test the actual installed ESP32 SHA implementation with small host API shims."""
Import("env")
from pathlib import Path

framework = Path(env.subst("$PROJECT_CORE_DIR")) / "packages/framework-arduinoespressif32"
if not (framework / "libraries/Hash/src/SHA2Builder.cpp").is_file():
    raise RuntimeError("native_library_test requires the project's ESP32 Arduino framework; build a board environment first")
core = framework / "cores/esp32"
hash_src = framework / "libraries/Hash/src"
env.Append(CPPPATH=[str(hash_src), str(core)])
project = Path(env.subst("$PROJECT_DIR"))
env.Prepend(CPPPATH=[str(project / "test/test_library_organization/support"), str(project / "test/support")])
if env.subst("$PIOENV") == "native_reading_status_test":
    env.Prepend(CPPPATH=[str(project / "test/test_reading_status/support")])
env.BuildSources("$BUILD_DIR/native_sha", str(hash_src), src_filter=["-<*>", "+<SHA2Builder.cpp>"])
env.BuildSources("$BUILD_DIR/native_hex", str(core), src_filter=["-<*>", "+<HEXBuilder.cpp>"])
