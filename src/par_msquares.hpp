#ifndef PAR_MSQUARES_H
#define PAR_MSQUARES_H

#include <stdint.h>


// -----------------------------------------------------------------------------
// BEGIN PUBLIC API
// -----------------------------------------------------------------------------
#ifndef PAR_MSQUARES_T
#define PAR_MSQUARES_T uint16_t
#endif

typedef uint8_t par_byte;

typedef struct par_msquares_meshlist_s par_msquares_meshlist;

// Results of a marching squares operation.  Triangles are counter-clockwise.
typedef struct {
    float* points;        // pointer to XY (or XYZ) vertex coordinates
    int npoints;          // number of vertex coordinates
    PAR_MSQUARES_T* triangles;  // pointer to 3-tuples of vertex indices
    int ntriangles;       // number of 3-tuples
    int dim;              // number of floats per point (either 2 or 3)
    uint32_t color;       // used only with par_msquares_color_multi
} par_msquares_mesh;

// Polyline boundary extracted from a mesh, composed of one or more chains.
// Counterclockwise chains are solid, clockwise chains are holes.  So, when
// serializing to SVG, all chains can be aggregated in a single <path>,
// provided they each terminate with a "Z" and use the default fill rule.
typedef struct {
    float* points;        // list of XY vertex coordinates
    int npoints;          // number of vertex coordinates
    float** chains;       // list of pointers to the start of each chain
    PAR_MSQUARES_T* lengths;    // list of chain lengths
    int nchains;          // number of chains
} par_msquares_boundary;

// Reverses the "insideness" test.
#define PAR_MSQUARES_INVERT (1 << 0)

// Returns a meshlist with two meshes: one for the inside, one for the outside.
#define PAR_MSQUARES_DUAL (1 << 1)

// Requests that returned meshes have 3-tuple coordinates instead of 2-tuples.
// When using a color-based function, the Z coordinate represents the alpha
// value of the nearest pixel.
#define PAR_MSQUARES_HEIGHTS (1 << 2)

// Applies a step function to the Z coordinates.  Requires HEIGHTS and DUAL.
#define PAR_MSQUARES_SNAP (1 << 3)

// Adds extrusion triangles to each mesh other than the lowest mesh.  Requires
// the PAR_MSQUARES_HEIGHTS flag to be present.
#define PAR_MSQUARES_CONNECT (1 << 4)

// Enables quick & dirty (not best) simpification of the returned mesh.
#define PAR_MSQUARES_SIMPLIFY (1 << 5)

// Indicates that the "color" argument is ABGR instead of ARGB.
#define PAR_MSQUARES_SWIZZLE (1 << 6)

// Ensures there are no T-junction vertices. (par_msquares_color_multi only)
// Requires the PAR_MSQUARES_SIMPLIFY flag to be disabled.
#define PAR_MSQUARES_CLEAN (1 << 7)

par_msquares_meshlist* par_msquares_grayscale(float const* data, int width,
    int height, int cellsize, float threshold, int flags);

par_msquares_meshlist* par_msquares_color(par_byte const* data, int width,
    int height, int cellsize, uint32_t color, int bpp, int flags);

par_msquares_mesh const* par_msquares_get_mesh(par_msquares_meshlist*, int n);

int par_msquares_get_count(par_msquares_meshlist*);

void par_msquares_free(par_msquares_meshlist*);

void par_msquares_free_boundary(par_msquares_boundary*);

typedef int (*par_msquares_inside_fn)(int, void*);
typedef float (*par_msquares_height_fn)(float, float, void*);

par_msquares_meshlist* par_msquares_function(int width, int height,
    int cellsize, int flags, void* context, par_msquares_inside_fn insidefn,
    par_msquares_height_fn heightfn);

par_msquares_meshlist* par_msquares_grayscale_multi(float const* data,
    int width, int height, int cellsize, float const* thresholds,
    int nthresholds, int flags);

par_msquares_meshlist* par_msquares_color_multi(par_byte const* data, int width,
    int height, int cellsize, int bpp, int flags);

par_msquares_boundary* par_msquares_extract_boundary(par_msquares_mesh const* );

#ifndef PAR_PI
#define PAR_PI (3.14159265359)
#define PAR_MIN(a, b) (a > b ? b : a)
#define PAR_MAX(a, b) (a > b ? a : b)
#define PAR_CLAMP(v, lo, hi) PAR_MAX(lo, PAR_MIN(hi, v))
#define PAR_SWAP(T, A, B) { T tmp = B; B = A; A = tmp; }
#define PAR_SQR(a) ((a) * (a))
#endif

#ifndef PAR_MALLOC
#define PAR_MALLOC(T, N) ((T*) malloc(N * sizeof(T)))
#define PAR_CALLOC(T, N) ((T*) calloc(N * sizeof(T), 1))
#define PAR_REALLOC(T, BUF, N) ((T*) realloc(BUF, sizeof(T) * (N)))
#define PAR_FREE(BUF) free(BUF)
#endif

// -----------------------------------------------------------------------------
// END PUBLIC API
// -----------------------------------------------------------------------------

#endif // PAR_MSQUARES_H

// par_msquares is distributed under the MIT license:
//
// Copyright (c) 2019 Philip Rideout
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.