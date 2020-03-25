#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

// colourmaps
#include "colourmap.h"

/*#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>*/

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>