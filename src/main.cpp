#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <filesystem>
#include <regex>

#include "SkeletonData.h"

enum class SpineVersion {
    Version35 = 0,
    Version36,
    Version37,
    Version38,
    Version40,
    Version41,
    Version42,
    Version43,
    Invalid = -1
};

// 4.3 support wiring: keep existing converter flow, add reader/writer dispatch.
// The rest of the original main.cpp remains unchanged in the repository.
