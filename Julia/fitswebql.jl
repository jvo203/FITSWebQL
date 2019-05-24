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
rbf_forward_pass = cl.Kernel(program, "rbf_forward_pass")

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

    σX = 0.1 / (XCLUST - 1)
    σY = 0.1 / (YCLUST - 1)
    θ = 2 * π * rand(NCLUST)    

    a = 0.5 * cos.(θ) .* cos.(θ) / (σX * σX) .+ 0.5 * sin.(θ) .* sin.(θ) / (σY * σY)
    b = -0.25 * sin.(2.0 * θ) / (σX * σX) .+ 0.25 * sin.(2.0 * θ) / (σY * σY)
    c = 0.5 * sin.(θ) .* sin.(θ) / (σX * σX) .+ 0.5 * cos.(θ) .* cos.(θ) / (σY * σY)
    
    p0 = log.(a)
    p1 = b
    p2 = log.(c)

    w = randn(Float32, NCLUST + 1)

    #gradients
    grad_c1 = zeros(Float32, NCLUST)
    grad_c2 = zeros(Float32, NCLUST)
    grad_p0 = zeros(Float32, NCLUST)
    grad_p1 = zeros(Float32, NCLUST)
    grad_p2 = zeros(Float32, NCLUST)
    grad_w = zeros(Float32, NCLUST + 1)

    #OpenCL buffers
    data_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = d)
    x1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x1)
    x2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x2)
    y_buff = cl.Buffer(Float32, ctx, :w, length(y))
    e_buff = cl.Buffer(Float32, ctx, :w, length(e))
    c1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = c1)
    c2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = c2)
    p0_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p0))
    p1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p1))
    p2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p2))
    w_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = w)
    grad_w_buff = cl.Buffer(Float32, ctx, :w, length(w))

    #execute a forward pass
    @time queue(rbf_forward_pass, size(d), nothing, x1_buff, x2_buff, y_buff, data_buff, e_buff, c1_buff, c2_buff, p0_buff, p1_buff, p2_buff, w_buff, grad_w_buff)

    y = cl.read(queue, y_buff)
    e = cl.read(queue, e_buff)
    grad_w = cl.read(queue, grad_w_buff)    

    println(grad_w)
end