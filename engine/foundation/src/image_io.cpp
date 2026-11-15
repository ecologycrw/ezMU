#include <ezmu/foundation/image_io.hpp>

#include <climits>
#include <cstddef>
#include <format>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <system_error>
#include <utility>

// stb is a private dependency of foundation (ADR 0021): only this file includes it. Its
// headers are on a system include path, so their warnings do not apply to ezMU code.
// NOLINTBEGIN(cppcoreguidelines-macro-usage): stb is configured through macros.
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
// NOLINTEND(cppcoreguidelines-macro-usage)
#include <stb_image.h>
#include <stb_image_write.h>

namespace ezmu::foundation {

namespace {

constexpr int kChannels = 4;

struct WriteContext {
    std::vector<std::uint8_t>* output = nullptr;
    bool failed = false;
};

// Called by stb (C code): must not throw.
void appendBytes(void* context, void* data, int size) noexcept {
    auto& ctx = *static_cast<WriteContext*>(context);
    if (ctx.failed || size <= 0) {
        return;
    }
    try {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        ctx.output->insert(ctx.output->end(), bytes, bytes + size);
    } catch (const std::bad_alloc&) {
        ctx.failed = true;
    }
}

} // namespace

Result<std::vector<std::uint8_t>> encodePng(const Image& image) {
    if (image.isEmpty()) {
        return makeError(ErrorCode::kInvalidArgument, "encodePng: image is empty");
    }
    constexpr auto kMaxDimension = static_cast<std::uint32_t>(INT_MAX / kChannels);
    if (image.width() > kMaxDimension || image.height() > kMaxDimension) {
        return makeError(
            ErrorCode::kInvalidArgument,
            std::format("encodePng: image {}x{} is too large", image.width(), image.height()));
    }

    std::vector<std::uint8_t> output;
    WriteContext context{.output = &output};
    const int width = static_cast<int>(image.width());
    const int height = static_cast<int>(image.height());
    const int ok = stbi_write_png_to_func(&appendBytes, &context, width, height, kChannels,
                                          image.bytes().data(), width * kChannels);
    if (ok == 0 || context.failed) {
        return makeError(ErrorCode::kInternal, "encodePng: PNG encoding failed");
    }
    return output;
}

Result<Image> decodePng(std::span<const std::uint8_t> data) {
    if (data.empty() || data.size() > static_cast<std::size_t>(INT_MAX)) {
        return makeError(ErrorCode::kParseError, "decodePng: invalid data size");
    }
    int width = 0;
    int height = 0;
    int channelsInFile = 0;
    stbi_uc* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width,
                                            &height, &channelsInFile, kChannels);
    if (pixels == nullptr) {
        return makeError(ErrorCode::kParseError,
                         std::format("decodePng: {}", stbi_failure_reason()));
    }
    const Extent2D size{.width = static_cast<std::uint32_t>(width),
                        .height = static_cast<std::uint32_t>(height)};
    const auto byteCount = static_cast<std::size_t>(size.area()) * Image::kBytesPerPixel;
    std::vector<std::uint8_t> bytes(pixels, pixels + byteCount);
    stbi_image_free(pixels);
    return Image::fromBytes(size, std::move(bytes));
}

Result<> writePng(const std::filesystem::path& path, const Image& image) {
    auto encoded = encodePng(image);
    if (!encoded) {
        return std::unexpected(std::move(encoded.error()));
    }
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return makeError(ErrorCode::kIoError,
                             std::format("writePng: cannot create '{}': {}",
                                         path.parent_path().string(), ec.message()));
        }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(encoded->data()), // NOLINT: byte I/O
               static_cast<std::streamsize>(encoded->size()));
    if (!file) {
        return makeError(ErrorCode::kIoError,
                         std::format("writePng: cannot write '{}'", path.string()));
    }
    return {};
}

Result<Image> readPng(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return makeError(ErrorCode::kNotFound,
                         std::format("readPng: '{}' does not exist", path.string()));
    }
    std::ifstream file(path, std::ios::binary);
    std::vector<std::uint8_t> data(std::istreambuf_iterator<char>(file), {});
    if (file.bad()) {
        return makeError(ErrorCode::kIoError,
                         std::format("readPng: cannot read '{}'", path.string()));
    }
    auto image = decodePng(data);
    if (!image) {
        return makeError(image.error().code(),
                         std::format("{} ('{}')", image.error().message(), path.string()));
    }
    return image;
}

} // namespace ezmu::foundation
