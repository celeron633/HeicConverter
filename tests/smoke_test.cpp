#include "converter.h"
#include "exif_metadata.h"
#include "image_writers.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

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

std::vector<uint8_t> ReadBytes(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream) {
        return {};
    }
    const std::streampos end = stream.tellg();
    if (end <= 0) {
        return {};
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(end));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return stream ? bytes : std::vector<uint8_t>{};
}

uint32_t ReadBigEndian32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24U) |
           (static_cast<uint32_t>(bytes[1]) << 16U) |
           (static_cast<uint32_t>(bytes[2]) << 8U) |
           static_cast<uint32_t>(bytes[3]);
}

bool PngContainsExif(const std::filesystem::path& file, const std::vector<uint8_t>& expected) {
    const std::vector<uint8_t> bytes = ReadBytes(file);
    size_t position = 8;
    while (position <= bytes.size() && bytes.size() - position >= 12) {
        const uint32_t length = ReadBigEndian32(bytes.data() + position);
        if (length > bytes.size() - position - 12U) {
            return false;
        }
        const uint8_t* type = bytes.data() + position + 4U;
        if (type[0] == 'e' && type[1] == 'X' && type[2] == 'I' && type[3] == 'f') {
            return length == expected.size() &&
                   std::equal(expected.begin(), expected.end(), bytes.begin() + static_cast<std::ptrdiff_t>(position + 8U));
        }
        position += 12U + length;
    }
    return false;
}

bool JpegContainsExif(const std::filesystem::path& file, const std::vector<uint8_t>& expected) {
    const std::vector<uint8_t> bytes = ReadBytes(file);
    const std::array<uint8_t, 6> header = {'E', 'x', 'i', 'f', 0, 0};
    for (size_t position = 0; position + header.size() + expected.size() <= bytes.size(); ++position) {
        if (std::equal(header.begin(), header.end(), bytes.begin() + static_cast<std::ptrdiff_t>(position)) &&
            std::equal(
                expected.begin(),
                expected.end(),
                bytes.begin() + static_cast<std::ptrdiff_t>(position + header.size()))) {
            return true;
        }
    }
    return false;
}

bool MetadataWritersPass(const std::filesystem::path& directory) {
    // Minimal little-endian TIFF data containing Orientation = 6.
    std::vector<uint8_t> exif = {
        'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00,
        0x01, 0x00,
        0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
    heic_converter::NormalizeExifMetadata(exif, 1, 1);
    if (exif[18] != 1 || exif[19] != 0) {
        return false;
    }

    constexpr std::array<uint8_t, 4> pixel = {20, 40, 60, 255};
    const std::filesystem::path png = directory / L"metadata.png";
    const std::filesystem::path jpeg = directory / L"metadata.jpg";
    std::string error;
    if (!heic_converter::WritePng(png, pixel.data(), 1, 1, 4, exif, Language::English, error) ||
        !PngContainsExif(png, exif)) {
        return false;
    }
    if (!heic_converter::WriteJpeg(jpeg, pixel.data(), 1, 1, 4, 90, exif, Language::English, error) ||
        !JpegContainsExif(jpeg, exif)) {
        return false;
    }
    return true;
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
        const std::array<std::wstring, 4> stems = {L"样例一", L"样例二", L"样例三", L"样例四"};
        std::vector<std::filesystem::path> inputs;
        std::vector<std::filesystem::path> pngOutputs;
        std::vector<std::filesystem::path> jpegOutputs;
        for (const std::wstring& stem : stems) {
            inputs.push_back(nested / (stem + L".HEIC"));
            pngOutputs.push_back(nested / (stem + L".png"));
            jpegOutputs.push_back(nested / (stem + L".jpg"));
            std::filesystem::copy_file(sample, inputs.back());
        }

        ConversionController controller;
        ConversionOptions options;
        options.folder = temporary.Path();
        options.recursive = true;
        options.deleteOriginals = true;
        options.workerCount = 3;
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
        const bool pngInputsDeleted = std::none_of(inputs.begin(), inputs.end(), [](const auto& input) {
            return std::filesystem::exists(input);
        });
        const bool pngOutputsValid = std::all_of(pngOutputs.begin(), pngOutputs.end(), [](const auto& output) {
            return HasPngSignature(output);
        });
        if (snapshot.total != inputs.size() || snapshot.succeeded != inputs.size() || snapshot.failed != 0 ||
            snapshot.workerCount != options.workerCount || snapshot.activeWorkers != 0 || !pngInputsDeleted ||
            !pngOutputsValid || snapshot.status != "转换完成") {
            std::cerr << "Unexpected result: total=" << snapshot.total << " succeeded=" << snapshot.succeeded
                      << " failed=" << snapshot.failed << " status=" << snapshot.status << '\n';
            for (const auto& line : snapshot.log) {
                std::cerr << line << '\n';
            }
            return 1;
        }

        for (const auto& input : inputs) {
            std::filesystem::copy_file(sample, input);
        }
        options.outputFormat = OutputFormat::Jpeg;
        options.jpegQuality = 87;
        options.language = Language::English;
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
        const bool jpegInputsDeleted = std::none_of(inputs.begin(), inputs.end(), [](const auto& input) {
            return std::filesystem::exists(input);
        });
        const bool jpegOutputsValid = std::all_of(jpegOutputs.begin(), jpegOutputs.end(), [](const auto& output) {
            return HasJpegSignature(output);
        });
        if (snapshot.total != inputs.size() || snapshot.succeeded != inputs.size() || snapshot.failed != 0 ||
            snapshot.workerCount != options.workerCount || snapshot.activeWorkers != 0 || !jpegInputsDeleted ||
            !jpegOutputsValid || snapshot.status != "Conversion complete" || snapshot.log.size() != inputs.size() ||
            std::any_of(snapshot.log.begin(), snapshot.log.end(), [](const std::string& line) {
                return line.rfind("Completed:", 0) != 0;
            })) {
            std::cerr << "Unexpected JPEG result: total=" << snapshot.total << " succeeded=" << snapshot.succeeded
                      << " failed=" << snapshot.failed << " status=" << snapshot.status << '\n';
            for (const auto& line : snapshot.log) {
                std::cerr << line << '\n';
            }
            return 1;
        }

        if (!MetadataWritersPass(temporary.Path())) {
            std::cerr << "PNG/JPEG EXIF metadata preservation failed.\n";
            return 1;
        }

        std::cout << "Parallel PNG/JPEG conversion, EXIF metadata, and delete-original behavior passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
