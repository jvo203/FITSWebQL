using FITSIO

include("rbf_compress.jl")

const TILE_SIZE = 256
#const XCLUST = min(Int(round(TILE_SIZE / 8)), 32)#/16
#const YCLUST = min(Int(round(TILE_SIZE / 8)), 32)#/16
#const NCLUST = XCLUST * YCLUST

dir = "/home/chris/ダウンロード"
file = "ALMA01030862.fits"
#file = "ALMA01157085.fits"
#file = "Cygnus_sp46_vs-150_ve100_dv0.50_CN_Tmb.fits.gz"

fitspath = homedir() * "/NAO/NRO/SF/orion_12co_all_SF7.5arcsec_dV1.0kms.fits"
#fitspath = string(dir, "/", file)

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

#for frame = 1:depth
for frame = 1:1
	global N

    if N == 4
        data = read(f[1], :, :, frame, :);
    elseif N == 3
        data = read(f[1], :, :, frame);
    elseif N == 2
        data = read(f[1], :, :);
    end

    #println("HDU $(frame): ", size(data))
    sub = view(data, :, :, 1, 1)
    println("frame : ", frame, "\tdims: ", size(sub))

    ncols = Int(ceil(width / TILE_SIZE))
    nrows = Int(ceil(height / TILE_SIZE))

    println("nrows: $(nrows),  ncols: $(ncols)")

    for row in nrows:nrows
        for col in ncols:ncols                    
            x₁ = (col - 1) * TILE_SIZE
            x₂ = min(width, x₁ + TILE_SIZE)
            y₁ = (row - 1) * TILE_SIZE
            y₂ = min(height, y₁ + TILE_SIZE)
            println("processing row $(row) column $(col) :> x₁=$(x₁) x₂=$(x₂) y₁=$(y₁) y₂=$(y₂)")
            tile = view(sub, (x₁+1):x₂, (y₁+1):y₂)            
            (N, padding, c1, c2, p0, p1, p2, w) = rbf_compress_tile(tile, device, ctx, queue)            
        end
    end
end

close(f)
