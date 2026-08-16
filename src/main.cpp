#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "SkeletonData.h"

namespace spine43 {
SkeletonData readBinaryData(const Binary&);
Binary writeBinaryData(SkeletonData&);
}

enum class SpineVersion {
    Version35 = 0,
    Version36 = 1,
    Version37 = 2,
    Version38 = 3,
    Version40 = 4,
    Version41 = 5,
    Version42 = 6,
    Version43 = 7,
    Invalid = -1
};

enum class FileFormat { Json, Skel, Unknown };

struct ConversionOptions {
    std::string inputFile;
    std::string outputFile;
    FileFormat inputFormat = FileFormat::Unknown;
    FileFormat outputFormat = FileFormat::Unknown;
    SpineVersion outputVersion = SpineVersion::Invalid;
    std::string outputVersionString;
    bool help = false;
    bool removeCurve = false;
};

bool aboveOrEqualVersion(SpineVersion version, SpineVersion target) {
    return static_cast<int>(version) >= static_cast<int>(target);
}

bool belowOrEqualVersion(SpineVersion version, SpineVersion target) {
    return static_cast<int>(version) <= static_cast<int>(target);
}

SpineVersion versionFromMajorMinor(const std::string& majorMinor) {
    if (majorMinor == "3.5") return SpineVersion::Version35;
    if (majorMinor == "3.6") return SpineVersion::Version36;
    if (majorMinor == "3.7") return SpineVersion::Version37;
    if (majorMinor == "3.8") return SpineVersion::Version38;
    if (majorMinor == "4.0") return SpineVersion::Version40;
    if (majorMinor == "4.1") return SpineVersion::Version41;
    if (majorMinor == "4.2") return SpineVersion::Version42;
    if (majorMinor == "4.3") return SpineVersion::Version43;
    return SpineVersion::Invalid;
}

SpineVersion detectSpineVersion(const std::string& filePath) {
    try {
        std::ifstream ifs(filePath, std::ios::binary);
        if (!ifs) return SpineVersion::Invalid;
        const size_t headerSize = 256;
        char buffer[headerSize] = {0};
        ifs.read(buffer, headerSize);
        std::string data(buffer, ifs.gcount());
        std::regex versionRegex(R"((\d+)\.(\d+)\.(\d+))");
        std::smatch match;
        if (std::regex_search(data, match, versionRegex))
            return versionFromMajorMinor(match[1].str() + "." + match[2].str());
    } catch (...) {
        std::cerr << "Error: Failed to read file: " << filePath << "\n";
    }
    return SpineVersion::Invalid;
}

std::string getVersionString(SpineVersion version) {
    switch (version) {
        case SpineVersion::Version35: return "3.5";
        case SpineVersion::Version36: return "3.6";
        case SpineVersion::Version37: return "3.7";
        case SpineVersion::Version38: return "3.8";
        case SpineVersion::Version40: return "4.0";
        case SpineVersion::Version41: return "4.1";
        case SpineVersion::Version42: return "4.2";
        case SpineVersion::Version43: return "4.3";
        default: return "Unknown";
    }
}

SpineVersion parseVersionString(const std::string& versionStr) {
    std::regex versionRegex(R"(^(\d+)\.(\d+)\.(\d+)$)");
    std::smatch match;
    if (!std::regex_match(versionStr, match, versionRegex)) return SpineVersion::Invalid;
    return versionFromMajorMinor(match[1].str() + "." + match[2].str());
}

SkeletonData readInputData(FileFormat format, SpineVersion version,
                           const Binary& binaryData, const Json& jsonData) {
    switch (version) {
        case SpineVersion::Version35:
            return format == FileFormat::Skel ? spine35::readBinaryData(binaryData) : spine35::readJsonData(jsonData);
        case SpineVersion::Version36:
            return format == FileFormat::Skel ? spine36::readBinaryData(binaryData) : spine36::readJsonData(jsonData);
        case SpineVersion::Version37:
            return format == FileFormat::Skel ? spine37::readBinaryData(binaryData) : spine37::readJsonData(jsonData);
        case SpineVersion::Version38:
            return format == FileFormat::Skel ? spine38::readBinaryData(binaryData) : spine38::readJsonData(jsonData);
        case SpineVersion::Version40:
            return format == FileFormat::Skel ? spine40::readBinaryData(binaryData) : spine40::readJsonData(jsonData);
        case SpineVersion::Version41:
            return format == FileFormat::Skel ? spine41::readBinaryData(binaryData) : spine41::readJsonData(jsonData);
        case SpineVersion::Version42:
            return format == FileFormat::Skel ? spine42::readBinaryData(binaryData) : spine42::readJsonData(jsonData);
        case SpineVersion::Version43:
            if (format != FileFormat::Skel)
                throw std::runtime_error("Spine 4.3 JSON input is not supported yet; use a 4.3 .skel export");
            return spine43::readBinaryData(binaryData);
        default:
            throw std::runtime_error("Unsupported input Spine version");
    }
}

template <typename Writer>
bool writeBinaryFile(const std::string& outputFile, SkeletonData& skelData, Writer writer) {
    auto outputData = writer(skelData);
    std::ofstream ofs(outputFile, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(outputData.data()), static_cast<std::streamsize>(outputData.size()));
    return static_cast<bool>(ofs);
}

template <typename Writer>
bool writeJsonFile(const std::string& outputFile, const SkeletonData& skelData, Writer writer) {
    auto outputJson = writer(skelData);
    std::ofstream ofs(outputFile);
    if (!ofs) return false;
    ofs << dumpJson(outputJson);
    return static_cast<bool>(ofs);
}

bool writeOutputData(const std::string& outputFile, FileFormat format,
                     SpineVersion version, SkeletonData& skelData) {
    bool ok = false;
    switch (version) {
        case SpineVersion::Version35:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine35::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine35::writeJsonData);
            break;
        case SpineVersion::Version36:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine36::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine36::writeJsonData);
            break;
        case SpineVersion::Version37:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine37::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine37::writeJsonData);
            break;
        case SpineVersion::Version38:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine38::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine38::writeJsonData);
            break;
        case SpineVersion::Version40:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine40::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine40::writeJsonData);
            break;
        case SpineVersion::Version41:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine41::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine41::writeJsonData);
            break;
        case SpineVersion::Version42:
            ok = format == FileFormat::Skel
                ? writeBinaryFile(outputFile, skelData, spine42::writeBinaryData)
                : writeJsonFile(outputFile, skelData, spine42::writeJsonData);
            break;
        case SpineVersion::Version43:
            if (format != FileFormat::Skel)
                throw std::runtime_error("Spine 4.3 JSON output is not supported yet; use .skel output");
            ok = writeBinaryFile(outputFile, skelData, spine43::writeBinaryData);
            break;
        default:
            throw std::runtime_error("Unsupported output Spine version");
    }
    if (!ok) std::cerr << "Error: Cannot create output file: " << outputFile << "\n";
    return ok;
}

bool convertFile(const std::string& inputFile, const std::string& outputFile,
                 FileFormat inputFormat, FileFormat outputFormat,
                 SpineVersion inputVersion, SpineVersion outputVersion,
                 const std::string& outputVersionString,
                 bool removeCurveOption) {
    try {
        Binary binaryData;
        Json jsonData;
        if (inputFormat == FileFormat::Skel) {
            std::ifstream ifs(inputFile, std::ios::binary);
            if (!ifs) {
                std::cerr << "Error: Cannot open input file: " << inputFile << "\n";
                return false;
            }
            binaryData.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
        } else {
            std::ifstream ifs(inputFile);
            if (!ifs) {
                std::cerr << "Error: Cannot open input file: " << inputFile << "\n";
                return false;
            }
            ifs >> jsonData;
        }

        SkeletonData skelData = readInputData(inputFormat, inputVersion, binaryData, jsonData);
        if (!outputVersionString.empty()) skelData.version = outputVersionString;

        if (aboveOrEqualVersion(inputVersion, SpineVersion::Version40) &&
            belowOrEqualVersion(outputVersion, SpineVersion::Version38)) {
            std::cout << "Converting 4.x proportional path spacing mode to length for 3.x...\n";
            convertSpacingMode4xTo3x(skelData);
            std::cout << "Converting Spine 4.x rotate timelines to 3.x-compatible shortest-path keys...\n";
            convertRotateTimeline4xTo3x(skelData);
            if (removeCurveOption) {
                std::cout << "Converting from 4.x to 3.x with --remove-curve, stripping curves...\n";
                removeCurve(skelData);
            } else {
                std::cout << "Converting from 4.x to 3.x, adjusting curve format from abs to rel...\n";
                convertCurve4xTo3x(skelData);
            }
        }
        if (belowOrEqualVersion(inputVersion, SpineVersion::Version38) &&
            aboveOrEqualVersion(outputVersion, SpineVersion::Version40)) {
            std::cout << "Converting Spine 3.x rotate timelines to 4.x-compatible absolute angles...\n";
            convertRotateTimeline3xTo4x(skelData);
            if (removeCurveOption) {
                std::cout << "Converting from 3.x to 4.x with --remove-curve, stripping curves...\n";
                removeCurve(skelData);
            } else {
                std::cout << "Converting from 3.x to 4.x, adjusting curve format from rel to abs...\n";
                convertCurve3xTo4x(skelData);
            }
        }
        if (aboveOrEqualVersion(inputVersion, SpineVersion::Version42) &&
            belowOrEqualVersion(outputVersion, SpineVersion::Version41)) {
            std::cout << "Converting from 4.2+ to below 4.2, adjusting constraint order...\n";
            convertOrder42ToBelow(skelData);
        }

        return writeOutputData(outputFile, outputFormat, outputVersion, skelData);
    } catch (const std::exception& e) {
        std::cerr << "Error during conversion: " << e.what() << "\n";
        return false;
    }
}

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <input_file> <output_file> [options]\n\n";
    std::cout << "Supported file formats:\n";
    std::cout << "  .json       Spine JSON format\n";
    std::cout << "  .skel       Spine binary (SKEL) format\n\n";
    std::cout << "Options:\n";
    std::cout << "  -v          Output version (must be complete: x.y.z format)\n";
    std::cout << "  --remove-curve  Strip animation curves instead of converting between formats\n";
    std::cout << "  --help      Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << programName << " input.skel output.json\n";
    std::cout << "  " << programName << " input.json output.skel\n";
    std::cout << "  " << programName << " input38.skel output43.skel -v 4.3.23\n";
    std::cout << "  " << programName << " input43.skel output42.skel -v 4.2.43\n\n";
    std::cout << "Supported Spine versions: 3.5.x, 3.6.x, 3.7.x, 3.8.x, 4.0.x, 4.1.x, 4.2.x, 4.3.x\n";
    std::cout << "Spine 4.3 binary input/output is supported for the shared converter model; 4.3 JSON is not yet supported.\n";
    std::cout << "Note: Version must be specified in complete x.y.z format (e.g., 4.2.43, not 4.2)\n";
    std::cout << "Input version detection is automatic based on file content.\n";
    std::cout << "Output version defaults to input version unless specified with -v.\n";
}

ConversionOptions parseArguments(int argc, char* argv[]) {
    ConversionOptions options;
    if (argc < 3) {
        options.help = true;
        return options;
    }
    options.inputFile = argv[1];
    options.outputFile = argv[2];

    const std::string inputExt = std::filesystem::path(options.inputFile).extension().string();
    const std::string outputExt = std::filesystem::path(options.outputFile).extension().string();
    if (inputExt == ".json") options.inputFormat = FileFormat::Json;
    else if (inputExt == ".skel") options.inputFormat = FileFormat::Skel;
    else {
        std::cerr << "Error: Unsupported input file extension: " << inputExt << "\n";
        options.help = true;
        return options;
    }
    if (outputExt == ".json") options.outputFormat = FileFormat::Json;
    else if (outputExt == ".skel") options.outputFormat = FileFormat::Skel;
    else {
        std::cerr << "Error: Unsupported output file extension: " << outputExt << "\n";
        options.help = true;
        return options;
    }

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-v") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -v requires a version argument\n";
                options.help = true;
                continue;
            }
            options.outputVersionString = argv[++i];
            options.outputVersion = parseVersionString(options.outputVersionString);
            if (options.outputVersion == SpineVersion::Invalid) {
                std::cerr << "Error: Invalid output version: " << options.outputVersionString << "\n";
                std::cerr << "Please specify complete version number (e.g., 3.7.94, 4.2.11, 4.3.23)\n";
                std::cerr << "Supported major versions: 3.5.x, 3.6.x, 3.7.x, 3.8.x, 4.0.x, 4.1.x, 4.2.x, 4.3.x\n";
                options.help = true;
            }
        } else if (arg == "--help") {
            options.help = true;
        } else if (arg == "--remove-curve") {
            options.removeCurve = true;
        } else {
            std::cerr << "Warning: Unknown option: " << arg << "\n";
        }
    }
    return options;
}

int main(int argc, char* argv[]) {
    ConversionOptions options = parseArguments(argc, argv);
    if (options.help) {
        printUsage(argv[0]);
        return 0;
    }
    if (!std::filesystem::exists(options.inputFile)) {
        std::cerr << "Error: Input file does not exist: " << options.inputFile << "\n";
        return 1;
    }

    const SpineVersion inputVersion = detectSpineVersion(options.inputFile);
    if (inputVersion == SpineVersion::Invalid) {
        std::cerr << "Error: Could not detect Spine version from input file\n";
        return 1;
    }
    const SpineVersion outputVersion = options.outputVersion != SpineVersion::Invalid ? options.outputVersion : inputVersion;
    std::cout << "Detected input Spine version: " << getVersionString(inputVersion) << "\n";
    if (inputVersion != outputVersion) {
        std::cout << "Converting to output Spine version: " << getVersionString(outputVersion);
        if (!options.outputVersionString.empty()) std::cout << " (" << options.outputVersionString << ")";
        std::cout << "\n";
    }
    if (options.removeCurve)
        std::cout << "Option --remove-curve enabled: curves will be stripped instead of converted when crossing 3.x/4.x.\n";
    std::cout << "Converting from " << (options.inputFormat == FileFormat::Json ? "JSON" : "SKEL")
              << " to " << (options.outputFormat == FileFormat::Json ? "JSON" : "SKEL") << "...\n";

    if (convertFile(options.inputFile, options.outputFile, options.inputFormat, options.outputFormat,
                    inputVersion, outputVersion, options.outputVersionString, options.removeCurve)) {
        std::cout << "Conversion completed successfully!\n";
        std::cout << "Output file: " << options.outputFile << "\n";
        return 0;
    }
    std::cerr << "Conversion failed!\n";
    return 1;
}
