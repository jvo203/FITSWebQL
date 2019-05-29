using FITSIO
using NaNMath
using OpenCL

const TILE_SIZE = 256
const XCLUST = 32
const YCLUST = 32
const NCLUST = XCLUST * YCLUST
const NITER = 500

dir = "/home/chris/ダウンロード"
file = "ALMA01030862.fits"
#file = "ALMA01157085.fits"
#file = "Cygnus_sp46_vs-150_ve100_dv0.50_CN_Tmb.fits.gz"
fitspath = string(dir, "/", file)

if size(ARGS)[1] == 0
    println("usage: julia compress.jl <FITS filename>")
    #exit()
else
    fitspath = ARGS[1]
end

println("FITS file to compress: $(fitspath)")

f = FITS(fitspath)
N = ndims(f[1])
println("ndims: ", N, ", size: ", size(f[1]))

header = read_header(f[1])

width = 0
height = 0
depth = 1

if haskey(header, "NAXIS1")
    width = header["NAXIS1"]
end

if haskey(header, "NAXIS2")
    height = header["NAXIS2"]
end

if haskey(header, "NAXIS3")
    depth = header["NAXIS3"]
end

println("width: $(width), height: $(height), depth: $(depth)")

if depth < 1
    println("depth must be >= 1")
    close(f)
    exit()
end

if width < TILE_SIZE && height < TILE_SIZE
    println("the width and/or the height must be >= $(TILE_SIZE)")
    close(f)
    exit()
end

device, ctx, queue = cl.create_compute_context()
println(device)

println("XCLUST : ", XCLUST, "\tYCLUST : ", YCLUST, "\tNCLUST : ", NCLUST)

compression_code = open("rbf.cl") do file    
    "#define NCLUST $(NCLUST)\n" * read(file, String)
end

program = cl.Program(ctx, source = compression_code) |> cl.build!
rbf_gradient_pass = cl.Kernel(program, "rbf_gradient_pass")
rbf_compute = cl.Kernel(program, "rbf_compute")

#for frame = 1:depth
for frame = 1:1
    data = read(f[1], :, :, frame, :);
    #println("HDU $(frame): ", size(data))
    sub = view(data, :, :, 1, 1)
    println("frame : ", frame, "\tdims: ", size(sub))

    ncols = Int(ceil(width / TILE_SIZE))
    nrows = Int(ceil(height / TILE_SIZE))

    println("nrows: $(nrows),  ncols: $(ncols)")

    for row in 1:nrows
        for col in 1:ncols                    
            x₁ = (col - 1) * TILE_SIZE
            x₂ = min(width, x₁ + TILE_SIZE)
            y₁ = (row - 1) * TILE_SIZE
            y₂ = min(height, y₁ + TILE_SIZE)
            println("processing row $(row) column $(col) :> x₁=$(x₁) x₂=$(x₂) y₁=$(y₁) y₂=$(y₂)")
        end
    end
end

close(f)