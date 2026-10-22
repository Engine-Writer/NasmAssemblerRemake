#include <CLI11/CLI11.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>
#include <unordered_map>
#include <filesystem>
#include <vector>

#include <compiler/assembler.hpp>
#include "utilities/stringutils.hpp"

using IrToAsmParseFn = int(*)(std::string &) noexcept;
namespace fs = std::filesystem;


int llvmPreprocessor(std::string &) noexcept {
    std::cerr << "ERROR: LLVM PREPROCESSOR IS NOT IMPLEMENTED\n";
    return 1;
}


static const std::unordered_map<std::string, IrToAsmParseFn> dispTableIrPreprocessor {
    { "ISX", [](std::string &) noexcept { return 0; } },
    { "LLVM", llvmPreprocessor }
};




int main(int argc, char *argv[]) {
    CLI::App trash{"Garbage to garbage assembler."};

    std::string inPath = "";
    std::string outPath = "a.bin";
    bool outputAssembly = false;
    std::string inputIr = "ISX";

    trash.add_option("input", inPath, "Source file")->required()->check(CLI::ExistingFile);
    trash.add_option("-o,--out", outPath, "Output file");

    trash.add_flag("-S", outputAssembly, "Output raw assembly (i.e. IR->ASM)");
    trash.add_option("--imode", inputIr, "Input IR rather than ASM");

    CLI11_PARSE(trash, argc, argv);

    auto preprocessorIt = dispTableIrPreprocessor.find(capitalizeStr(inputIr));
    if (preprocessorIt == dispTableIrPreprocessor.end()) {
        std::cerr << "Unrecognized input mode: " << inputIr << '\n';
        return 1;
    }

    if (!fs::exists(inPath)) {
        std::cerr << "Path: " << inPath << "Does not exist? (this literally shouldnt be";
        std::cerr << " possible because CLI11 confirmed file does infact exist? huh?)\n";
        return 2;
    }

    std::ifstream inFileStream(inPath);
    std::ostringstream inFileSS;
    inFileSS << inFileStream.rdbuf();
    std::string inFile;
    inFile = inFileSS.str();

    int preprocessResult = preprocessorIt->second(inFile);

    if (preprocessResult) {
        std::cerr << "Preprocessor failed.\n";
        return 3;
    }

    if (outputAssembly) {
        std::cout << "Writing raw assembly\n";
        std::ofstream outFileStream(outPath);
        outFileStream << inFile;
        return 0;
    }

    auto assemblyOutput(assemblerMain(inFile));
    if (assemblyOutput.error) {
        std::cerr << "Assembly failed\n" << assemblyOutput.errTxt << '\n';
        return assemblyOutput.error;
    } else {
        std::ofstream outFileStream(outPath, std::ios::binary);
        outFileStream.write(reinterpret_cast<char *>(
            assemblyOutput.main.data()), assemblyOutput.main.size()
        );
    }

    return 0;
}
