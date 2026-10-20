#pragma once

#include <string>
#include <vector>

struct DecompileResult
{
    bool ok = false;
    std::string error;
    std::string folder;  // where the files went
    std::string qcPath;
    int smdFiles = 0;
    int textures = 0;
    std::vector<std::vector<std::string>> bodyFiles;
    std::vector<std::string> textureFiles; 
    std::vector<std::string> warnings;
};


DecompileResult DecompileStudioModel(const std::string& mdlPath, const std::string& outputDir);
