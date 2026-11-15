// ezmu_clear — "window clears the screen" (milestone M1, docs/specs/rhi.md).
//
// Composition root: the main thread owns the platform and the window and pumps OS events; a
// dedicated render thread (ADR 0012) owns the swapchain and clears it every frame. The two talk
// only through a small Mailbox (pixel size, minimized, capture) and the render thread's stop
// token (quit).
//
//   ezmu_clear [--frames N] [--capture out.png] [--color r,g,b[,a]] [--validation off|on|sync]
//
// Keys: Escape quits, F12 captures the next frame. Exit codes: 0 success, 1 failure,
// 77 skipped (no display or no usable Vulkan device; with EZMU_REQUIRE_GPU=1 a missing Vulkan
// device is a failure).

#include <ezmu/foundation/log.hpp>
#include <ezmu/foundation/result.hpp>
#include <ezmu/foundation/version.hpp>
#include <ezmu/platform/event.hpp>
#include <ezmu/platform/keys.hpp>
#include <ezmu/platform/platform.hpp>
#include <ezmu/platform/window.hpp>
#include <ezmu/rhi/device.hpp>
#include <ezmu/rhi/swapchain.hpp>

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "mailbox.hpp"
#include "render_loop.hpp"

namespace {

namespace foundation = ezmu::foundation;
namespace platform = ezmu::platform;
namespace rhi = ezmu::rhi;
namespace clear = ezmu::samples::clear;

constexpr int kExitOk = 0;
constexpr int kExitFailure = 1;
constexpr int kExitSkipped = 77; // CTest SKIP_RETURN_CODE convention.
constexpr std::string_view kCategory = "clear";
constexpr std::string_view kUsage =
    "usage: ezmu_clear [--frames N] [--capture out.png] [--color r,g,b[,a]] "
    "[--validation off|on|sync]";

struct Options {
    bool help = false;
    std::uint64_t frames = 0;
    std::filesystem::path capturePath{};
    std::array<float, 4> clearColor{0.1F, 0.2F, 0.4F, 1.0F};
    rhi::ValidationMode validation = rhi::kDefaultValidationMode;
};

[[nodiscard]] std::string_view toString(rhi::ValidationMode mode) noexcept {
    switch (mode) {
    case rhi::ValidationMode::kOff:
        return "off";
    case rhi::ValidationMode::kEnabled:
        return "on";
    case rhi::ValidationMode::kEnabledWithSync:
        return "on+sync";
    }
    return "?";
}

[[nodiscard]] std::optional<std::array<float, 4>> parseColor(std::string_view text) {
    std::array<float, 4> color{0.0F, 0.0F, 0.0F, 1.0F};
    std::size_t count = 0;
    const char* it = text.data();
    const char* const end = text.data() + text.size();
    while (count < color.size()) {
        const auto [next, error] = std::from_chars(it, end, color.at(count));
        if (error != std::errc{} || color.at(count) < 0.0F || color.at(count) > 1.0F) {
            return std::nullopt;
        }
        ++count;
        it = next;
        if (it == end) {
            break;
        }
        if (*it != ',') {
            return std::nullopt;
        }
        ++it;
    }
    if (it != end || count < 3) {
        return std::nullopt;
    }
    return color;
}

[[nodiscard]] std::optional<rhi::ValidationMode> parseValidation(std::string_view text) {
    if (text == "off") {
        return rhi::ValidationMode::kOff;
    }
    if (text == "on") {
        return rhi::ValidationMode::kEnabled;
    }
    if (text == "sync") {
        return rhi::ValidationMode::kEnabledWithSync;
    }
    return std::nullopt;
}

/// Applies one `--name value` pair; returns an error message for invalid input.
[[nodiscard]] std::optional<std::string> applyOption(Options& options, std::string_view name,
                                                     std::string_view value) {
    if (name == "--frames") {
        const char* const first = value.data();
        const char* const end = first + value.size();
        const auto [last, error] = std::from_chars(first, end, options.frames);
        if (error != std::errc{} || last != end) {
            return std::format("invalid frame count: {}", value);
        }
    } else if (name == "--capture") {
        options.capturePath = std::filesystem::path(value);
    } else if (name == "--color") {
        const auto color = parseColor(value);
        if (!color) {
            return std::format("invalid color (expected r,g,b[,a] in 0..1): {}", value);
        }
        options.clearColor = *color;
    } else if (name == "--validation") {
        const auto mode = parseValidation(value);
        if (!mode) {
            return std::format("invalid validation mode: {}", value);
        }
        options.validation = *mode;
    } else {
        return std::format("unknown option: {}", name);
    }
    return std::nullopt;
}

[[nodiscard]] foundation::Result<Options> parseOptions(std::span<char* const> argv) {
    const std::vector<std::string_view> args(argv.begin(), argv.end());
    Options options;
    for (std::size_t i = 1; i < args.size(); i += 2) {
        const std::string_view name = args.at(i);
        if (name == "--help" || name == "-h") {
            options.help = true;
            return options;
        }
        std::optional<std::string> error = i + 1 < args.size()
                                               ? applyOption(options, name, args.at(i + 1))
                                               : std::format("missing value for {}", name);
        if (error) {
            return foundation::makeError(foundation::ErrorCode::kInvalidArgument,
                                         std::format("{}\n{}", *error, kUsage));
        }
    }
    return options;
}

[[nodiscard]] std::string environmentVariable(const char* name) {
#ifdef _MSC_VER
    char* buffer = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&buffer, &length, name) != 0 || buffer == nullptr) {
        return {};
    }
    std::string value(buffer);
    std::free(buffer); // NOLINT(cppcoreguidelines-no-malloc): allocated by _dupenv_s.
    return value;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read at startup.
    return value != nullptr ? std::string(value) : std::string();
#endif
}

/// Exit code for a startup failure: "skipped" when the machine lacks the capability (unless
/// `gpu` and EZMU_REQUIRE_GPU=1), "failure" otherwise.
[[nodiscard]] int startupFailure(foundation::Logger& logger, std::string_view what,
                                 const foundation::Error& error, bool environmental, bool gpu) {
    const bool required = gpu && environmentVariable("EZMU_REQUIRE_GPU") == "1";
    if (environmental && !required) {
        logger.warning(kCategory, "skipped: {}: {}", what, error.message());
        return kExitSkipped;
    }
    logger.error(kCategory, "{}: {} ({})", what, error.message(), toString(error.code()));
    return kExitFailure;
}

/// Main-thread reaction to each platform event: everything goes to the mailbox or the render
/// thread's stop source.
struct EventHandler {
    const platform::Window* window = nullptr;
    clear::Mailbox* mailbox = nullptr;
    std::stop_source quit{};

    void operator()(const platform::QuitRequested& /*event*/) { quit.request_stop(); }
    void operator()(const platform::WindowCloseRequested& /*event*/) { quit.request_stop(); }
    void operator()(const platform::WindowResized& event) const {
        if (event.window == window->id()) {
            mailbox->publishPixelSize(event.pixelSize);
        }
    }
    void operator()(const platform::WindowMinimized& /*event*/) const {
        mailbox->setMinimized(true);
    }
    void operator()(const platform::WindowRestored& /*event*/) const {
        mailbox->publishPixelSize(window->pixelSize());
        mailbox->setMinimized(false);
    }
    void operator()(const platform::KeyDown& event) {
        if (event.scancode == platform::Scancode::kEscape) {
            quit.request_stop();
        } else if (event.scancode == platform::Scancode::kF12 && !event.repeat) {
            mailbox->requestCapture();
        }
    }
    void operator()(const auto& /*event*/) const {}
};

/// Pumps window events until the window closes or the render thread finishes.
void runMainLoop(platform::Platform& os, const platform::Window& window, clear::Mailbox& mailbox,
                 std::jthread& renderThread, const std::atomic<bool>& renderFinished) {
    using namespace std::chrono_literals;
    EventHandler handler{
        .window = &window, .mailbox = &mailbox, .quit = renderThread.get_stop_source()};
    while (!renderFinished.load(std::memory_order_acquire) && !handler.quit.stop_requested()) {
        for (const platform::Event& event : os.waitEvents(20ms)) {
            std::visit(handler, event);
        }
    }
}

[[nodiscard]] int run(std::span<char* const> args) {
    foundation::Logger logger;
    logger.addSink(std::make_unique<foundation::StderrLogSink>());

    auto options = parseOptions(args);
    if (!options) {
        std::println(stderr, "{}", options.error().message());
        return kExitFailure;
    }
    if (options->help) {
        std::println("{}", kUsage);
        return kExitOk;
    }

    auto createdPlatform = platform::Platform::create({.appName = "ezmu_clear"});
    if (!createdPlatform) {
        return startupFailure(logger, "no display", createdPlatform.error(), true, false);
    }
    std::unique_ptr<platform::Platform> os = std::move(*createdPlatform);

    auto createdWindow = os->createWindow({.title = "ezMU clear"});
    if (!createdWindow) {
        // Typically no Vulkan library (SDL loads it for Vulkan windows).
        return startupFailure(logger, "cannot create a Vulkan window", createdWindow.error(), true,
                              true);
    }
    std::unique_ptr<platform::Window> window = std::move(*createdWindow);

    // Counted here rather than through Device, so messages emitted while the device is
    // destroyed count too. Called from whichever thread made the Vulkan call.
    std::atomic<std::uint32_t> validationMessages{0};
    auto createdDevice = rhi::Device::create(
        {.appName = "ezmu_clear",
         .instanceExtensions = window->vulkanInstanceExtensions(),
         .requirePresent = true,
         .validation = options->validation,
         .onValidationMessage =
             [&](const rhi::ValidationMessage& message) {
                 validationMessages.fetch_add(1, std::memory_order_relaxed);
                 logger.log(message.severity == rhi::ValidationSeverity::kError
                                ? foundation::LogLevel::kError
                                : foundation::LogLevel::kWarning,
                            "vulkan", "[{}] {}", message.messageIdName, message.text);
             },
         .vkGetInstanceProcAddr = window->vulkanGetInstanceProcAddr()});
    if (!createdDevice) {
        return startupFailure(logger, "no usable Vulkan device", createdDevice.error(),
                              createdDevice.error().code() == foundation::ErrorCode::kUnsupported,
                              true);
    }
    std::unique_ptr<rhi::Device> device = std::move(*createdDevice);
    const rhi::AdapterInfo& adapter = device->adapterInfo();
    logger.info(kCategory, "{} {} on {} ({}, {}, Vulkan {}.{}.{}), validation {}",
                foundation::kEngineName, foundation::kEngineVersionString, adapter.name,
                rhi::toString(adapter.type), adapter.driver, adapter.apiVersion.major,
                adapter.apiVersion.minor, adapter.apiVersion.patch, toString(options->validation));

    auto surface = window->createVulkanSurface(device->nativeInstance());
    if (!surface) {
        return startupFailure(logger, "cannot create a Vulkan surface", surface.error(), false,
                              true);
    }
    auto createdSwapchain = device->createSwapchain(
        *surface, {.pixelSize = window->pixelSize(), .debugName = "ezmu_clear window"});
    if (!createdSwapchain) {
        return startupFailure(
            logger, "cannot create a swapchain", createdSwapchain.error(),
            createdSwapchain.error().code() == foundation::ErrorCode::kUnsupported, true);
    }
    std::unique_ptr<rhi::Swapchain> swapchain = std::move(*createdSwapchain);
    logger.info(kCategory, "swapchain {}x{} {} {}", swapchain->extent().width,
                swapchain->extent().height, rhi::toString(swapchain->format()),
                rhi::toString(swapchain->presentMode()));

    const clear::RenderConfig config{
        .clearColor = options->clearColor,
        .frameLimit = options->frames,
        .capturePath = options->capturePath.empty() ? std::filesystem::path("ezmu_clear.png")
                                                    : options->capturePath,
        .captureLastFrame = !options->capturePath.empty() && options->frames != 0};
    clear::Mailbox mailbox(window->pixelSize());
    std::atomic<bool> renderFinished{false};
    foundation::Result<> renderResult;
    {
        // The Device moves to the render thread (thread start is the happens-before edge) and
        // is not touched here again until join().
        std::jthread renderThread(
            [&, ownedSwapchain = std::move(swapchain)](const std::stop_token& stop) mutable {
                renderResult = clear::runRenderLoop(stop, *device, std::move(ownedSwapchain),
                                                    config, mailbox, logger);
                renderFinished.store(true, std::memory_order_release);
            });
        runMainLoop(*os, *window, mailbox, renderThread, renderFinished);
        renderThread.request_stop();
    } // join(): the swapchain and its surface are gone before the window.

    // Shutdown order: swapchain (done, with its surface), device, window, platform. The device
    // goes before the window because it was loaded through the window's
    // vkGetInstanceProcAddr: destroying the last Vulkan window makes SDL unload the Vulkan
    // library, and the device's teardown would then call into unloaded code.
    device.reset();
    window.reset();
    os.reset();

    if (!renderResult) {
        logger.error(kCategory, "render thread: {} ({})", renderResult.error().message(),
                     toString(renderResult.error().code()));
        return kExitFailure;
    }
    if (const std::uint32_t count = validationMessages.load(); count != 0) {
        logger.error(kCategory, "{} validation message(s) reported", count);
        return kExitFailure;
    }
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(std::span<char* const>(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception& e) {
        std::fputs(e.what(), stderr);
        std::fputc('\n', stderr);
        return kExitFailure;
    }
}
