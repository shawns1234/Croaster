# PlatformIO library extra script (library.json "build.extraScript").
# Host builds (platform = native, the unit-test env) compile only src/core:
# src/app and src/hw need Arduino-ESP32 and ESP-IDF headers.
Import("env")

if env.get("PIOPLATFORM") == "native":
    env.Replace(SRC_FILTER=["+<core/>"])
