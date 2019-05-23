using AstroImages
#] pkg> add https://github.com/JuliaAstro/AstroImages.jl

using FITSIO
using NaNMath; nm = NaNMath
using OpenCL
using Plots
using Random
using Statistics
using Wavelets

dir = "/home/chris/ダウンロード"
#file = "e20121211_0010500001_dp_sf_st_mos.fits"
#file = "ALMA01030862.fits"
file = "ALMA01157085.fits"
#file = "Cygnus_sp46_vs-150_ve100_dv0.50_CN_Tmb.fits.gz" #good for testing noisy datasets

#dir = "/home/chris/NAO/NRO/SF/"

fitspath = string(dir, "/", file)

#fitspath = "/home/chris/NAO/SubaruWebQL/FITSCACHE/SUPM139C52520E922500.fits"
#fitspath = "/home/chris/projects/fits_web_ql/FITSCACHE/d61977e0-fada-3cee-8198-31c00ea64ab3.fits"

println(ARGS, "\targc:", size(ARGS)[1])

if size(ARGS)[1] > 0
    fitspath = ARGS[1]
end

println("FITS file:", fitspath)

#fits = load(fitspath)
#img = AstroImage(fitspath)
#plot(img)

fits = FITS(fitspath)
println(fits)
println(fits[1])

data = @time read(fits[1])
dim = minimum(size(data))
println(size(data), " the smaller dim:", dim)
range = @time nm.extrema(data)
println("min,max:", range)
med = @time median(data)
println("median:", med)

#=
wt = wavelet(WT.cdf97, WT.Lifting)
# a full wavelet transform
#... array must be square/cube ...
sqdata = @view data[1:dim,1:dim]
#@time dwt!(sqdata, wt) #no support for ::GLS Float32 in-place transform
xt = @time dwt(sqdata, wt)
println(size(xt))

# wavelet packet transform
# execution errors (GSL only supports double!!!)
wt = wavelet(WT.db2, WT.Lifting)
L = 4
@time wpt(sqdata, wt, L)
=#

dims = size(data)
width = dims[1]
height = dims[2]
depth = dims[3]
capacity = width * height

XCLUST = Int(round(width / 16))
YCLUST = Int(round(height / 16))
NCLUST = XCLUST * YCLUST

println("width : ", width, "\theight : ", height, "\tdepth : ", depth, "\tcapacity : ", capacity)
println("XCLUST : ", XCLUST, "\tYCLUST : ", YCLUST, "\tNCLUST : ", NCLUST)

device, ctx, queue = cl.create_compute_context()
println(device)

compression_code = open("rbf.cl") do file
    #define NCLUST dynamically
    "#define NCLUST $(NCLUST)\n" * read(file, String)
end

program = cl.Program(ctx, source = compression_code) |> cl.build!

for frame = 1:1#1:depth
    sub = view(data, :, :, frame, 1)
    println("frame : ", frame, "\tdims: ", size(sub))
    (frame_min, frame_max) = @time nm.extrema(sub)

    x1 = Float32[]
    x2 = Float32[]
    d = Float32[]
    y = Float32[]
    e = Float32[]
    count = 0

    for iy = 1:height
        for ix = 1:width
            tmp = sub[ix,iy]
            if !isnan(tmp)
                count = count + 1
                pixel = log(0.5 + (tmp - frame_min) / (frame_max - frame_min))
                push!(d, pixel)
                push!(x1, (ix - 1) / (width - 1))   #[0,1]
                push!(x2, (iy - 1) / (height - 1))  #[0,1]
                push!(y, 0)
                push!(e, 0)
            end
        end
    end

    println("count : ", count)
    
    #training data OK

    #parameter initialisation
    c1 = shuffle(x1)[1:NCLUST]
    c2 = shuffle(x2)[1:NCLUST]    
end