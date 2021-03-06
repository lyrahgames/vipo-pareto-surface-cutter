#pragma once

#include <lyrahgames/delaunay/geometry.hpp>
//
#include <lyrahgames/delaunay/guibas_stolfi/triangulator.hpp>

// Default Triangulator
namespace lyrahgames::delaunay {
using triangulator = guibas_stolfi::triangulator;
}
