"""Bridge the simulator HAL to the firmware HAL wake-verification API.

The current CrossPoint simulator exposes a host-only zero-argument
``verifyPowerButtonWakeup`` helper, while YACP's firmware HAL uses the
two-argument production signature.  Keep the simulator behavior unchanged and
add the production-shaped overload after PlatformIO installs the dependency.
The patch is idempotent and only touches the active simulator dependency.
"""

Import("env")  # noqa: F821 - SCons injects this at build time

from pathlib import Path


def patch_simulator(env):
    project_dir = Path(env["PROJECT_DIR"])
    env_name = env.subst("$PIOENV")
    source_dir = project_dir / ".pio" / "libdeps" / env_name / "simulator" / "src"
    header_path = source_dir / "HalGPIO.h"
    source_path = source_dir / "HalGPIO.cpp"
    display_path = source_dir / "HalDisplay.cpp"
    arduino_path = source_dir / "Arduino.h"
    if (
        not header_path.is_file()
        or not source_path.is_file()
        or not display_path.is_file()
        or not arduino_path.is_file()
    ):
        return

    header = header_path.read_text(encoding="utf-8")
    declaration = "  void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);"
    # Current simulator releases already expose the production-shaped overload,
    # sometimes wrapped across lines and returning bool. Both forms are callable
    # by YACP, so only patch the legacy zero-argument API.
    if "verifyPowerButtonWakeup(uint16_t" not in header:
        marker = "  bool verifyPowerButtonWakeup();"
        if marker not in header:
            raise RuntimeError("Simulator HalGPIO wake-verification API was not recognized")
        header = header.replace(marker, marker + "\n" + declaration, 1)
        header_path.write_text(header, encoding="utf-8")

    source = source_path.read_text(encoding="utf-8")
    definition = (
        "void HalGPIO::verifyPowerButtonWakeup(uint16_t /*requiredDurationMs*/, "
        "bool /*shortPressAllowed*/) {\n"
        "  // The simulator has no physical wake button to validate.\n"
        "}\n"
    )
    if "HalGPIO::verifyPowerButtonWakeup(uint16_t" not in source:
        marker = "bool HalGPIO::verifyPowerButtonWakeup()"
        marker_position = source.find(marker)
        if marker_position < 0:
            raise RuntimeError("Simulator HalGPIO wake-verification implementation was not recognized")
        source = source[:marker_position] + definition + source[marker_position:]
        source_path.write_text(source, encoding="utf-8")

    display = display_path.read_text(encoding="utf-8")
    accelerated = "  sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);"
    fallback = (
        accelerated
        + "\n  if (!sdl_renderer) {\n"
        + "    sdl_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);\n"
        + "  }"
    )
    if fallback not in display:
        if accelerated not in display:
            raise RuntimeError("Simulator SDL renderer initialization was not recognized")
        display = display.replace(accelerated, fallback, 1)
        display_path.write_text(display, encoding="utf-8")

    # Restore the configurable heap readings used by YACP's constrained-memory
    # regression suite. Newer simulator releases hard-code 1 MiB, which prevents
    # the ESP32-C3 safety guards and adaptive pagination path from being exercised.
    arduino = arduino_path.read_text(encoding="utf-8")
    old_heap = """struct ESPMock {
  uint32_t getFreeHeap() { return 1024 * 1024; }
  void restart() {}
  uint32_t getHeapSize() { return 1024 * 1024; }
  uint32_t getMinFreeHeap() { return 1024 * 1024; }
  uint32_t getMaxAllocHeap() { return 1024 * 1024; }
};"""
    new_heap = """inline uint32_t simulatorHeapValue(const char *name) {
  const char *raw = std::getenv(name);
  if (!raw || !raw[0]) return 1024 * 1024;
  const unsigned long value = std::strtoul(raw, nullptr, 10);
  return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

struct ESPMock {
  uint32_t getFreeHeap() { return simulatorHeapValue(\"CROSSPOINT_SIM_FREE_HEAP\"); }
  void restart() {}
  uint32_t getHeapSize() { return simulatorHeapValue(\"CROSSPOINT_SIM_FREE_HEAP\"); }
  uint32_t getMinFreeHeap() { return simulatorHeapValue(\"CROSSPOINT_SIM_FREE_HEAP\"); }
  uint32_t getMaxAllocHeap() { return simulatorHeapValue(\"CROSSPOINT_SIM_MAX_ALLOC_HEAP\"); }
};"""
    has_configurable_heap = "simulatorHeapValue" in arduino or 'heapValue("CROSSPOINT_SIM_FREE_HEAP")' in arduino
    if not has_configurable_heap:
        if old_heap not in arduino:
            raise RuntimeError("Simulator heap API was not recognized")
        arduino = arduino.replace(old_heap, new_heap, 1)
        arduino_path.write_text(arduino, encoding="utf-8")


patch_simulator(env)  # noqa: F821
