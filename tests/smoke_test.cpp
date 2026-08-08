#include "converter.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {
        if (!std::filesystem::create_directory(path_)) {
            throw std::runtime_error("Could not create the smoke-test directory.");
        }
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool HasPngSignature(const std::filesystem::path& file) {
    constexpr std::array<unsigned char, 8> expected = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    std::array<unsigned char, 8> actual{};
    std::ifstream stream(file, std::ios::binary);
    stream.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    return stream.gcount() == static_cast<std::streamsize>(actual.size()) && actual == expected;
}

bool HasJpegSignature(const std::filesystem::path& file) {
    constexpr std::array<unsigned char, 3> expected = {0xFF, 0xD8, 0xFF};
    std::array<unsigned char, 3> actual{};
    std::ifstream stream(file, std::ios::binary);
    stream.read(reinterpret_cast<char*>(actual.data()), static_cast<std::streamsize>(actual.size()));
    return stream.gcount() == static_cast<std::streamsize>(actual.size()) && actual == expected;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: HeicConverterSmokeTest <sample.heic>\n";
        return 2;
    }

    try {
        const std::filesystem::path sample(argv[1]);
        if (!std::filesystem::is_regular_file(sample)) {
            std::cerr << "Sample file does not exist.\n";
            return 2;
        }

        std::filesystem::path directory = std::filesystem::temp_directory_path();
        directory /= L"HeicConverter-测试-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64());
        TemporaryDirectory temporary(directory);

        const std::filesystem::path nested = temporary.Path() / L"子目录";
        std::filesystem::create_directory(nested);
        const std::filesystem::path input = nested / L"样例.HEIC";
        const std::filesystem::path output = nested / L"样例.png";
        std::filesystem::copy_file(sample, input);

        ConversionController controller;
        ConversionOptions options;
        options.folder = temporary.Path();
        options.recursive = true;
        options.deleteOriginals = true;
        if (!controller.Start(options)) {
            std::cerr << "Could not start conversion.\n";
            return 1;
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        ConversionSnapshot snapshot;
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            snapshot = controller.Snapshot();
        } while (snapshot.running && std::chrono::steady_clock::now() < deadline);

        if (snapshot.running) {
            controller.Cancel();
            std::cerr << "Conversion timed out.\n";
            return 1;
        }
        if (snapshot.total != 1 || snapshot.succeeded != 1 || snapshot.failed != 0 || std::filesystem::exists(input) ||
            !HasPngSignature(output)) {
            std::cerr << "Unexpected result: total=" << snapshot.total << " succeeded=" << snapshot.succeeded
                      << " failed=" << snapshot.failed << " status=" << snapshot.status << '\n';
            for (const auto& line : snapshot.log) {
                std::cerr << line << '\n';
            }
            return 1;
        }

        const std::filesystem::path jpegOutput = nested / L"样例.jpg";
        std::filesystem::copy_file(sample, input);
        options.outputFormat = OutputFormat::Jpeg;
        options.jpegQuality = 87;
        if (!controller.Start(options)) {
            std::cerr << "Could not start JPEG conversion.\n";
            return 1;
        }

        const auto jpegDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            snapshot = controller.Snapshot();
        } while (snapshot.running && std::chrono::steady_clock::now() < jpegDeadline);

        if (snapshot.running) {
            controller.Cancel();
            std::cerr << "JPEG conversion timed out.\n";
            return 1;
        }
        if (snapshot.total != 1 || snapshot.succeeded != 1 || snapshot.failed != 0 || std::filesystem::exists(input) ||
            !HasJpegSignature(jpegOutput)) {
            std::cerr << "Unexpected JPEG result: total=" << snapshot.total << " succeeded=" << snapshot.succeeded
                      << " failed=" << snapshot.failed << " status=" << snapshot.status << '\n';
            for (const auto& line : snapshot.log) {
                std::cerr << line << '\n';
            }
            return 1;
        }

        std::cout << "Recursive PNG/JPEG conversion and delete-original behavior passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
