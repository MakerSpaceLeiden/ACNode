#include "util/jsonAllocator.h"

// We see actual peaks at around 3.9-4 kByte.
//
SpiRamAllocator jsonAllocator(8 * 1024);
