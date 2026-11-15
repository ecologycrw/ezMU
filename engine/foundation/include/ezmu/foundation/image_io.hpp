#pragma once

#include <ezmu/foundation/image.hpp>
#include <ezmu/foundation/result.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

// PNG encoding and decoding of Images (ADR 0021). Intended for tests, frame capture and debug
// output, not for game texture assets. All functions are thread-safe (no shared state) and
// allocate the returned buffers.

namespace ezmu::foundation {

/// Encodes `image` as an RGBA8 PNG. Fails with kInvalidArgument for an empty image.
[[nodiscard]] Result<std::vector<std::uint8_t>> encodePng(const Image& image);

/// Decodes a PNG (any bit depth or color type) into RGBA8. Fails with kParseError on invalid
/// data.
[[nodiscard]] Result<Image> decodePng(std::span<const std::uint8_t> data);

/// Encodes and writes `image` to `path`, creating parent directories. Fails with kIoError.
[[nodiscard]] Result<> writePng(const std::filesystem::path& path, const Image& image);

/// Reads and decodes the PNG at `path`. Fails with kNotFound, kIoError or kParseError.
[[nodiscard]] Result<Image> readPng(const std::filesystem::path& path);

} // namespace ezmu::foundation
