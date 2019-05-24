using AstroImages
#] pkg> add https://github.com/JuliaAstro/AstroImages.jl

using FITSIO
using LinearAlgebra
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

function rbf_forward_pass_julia(_x1, _x2, _y, _data, _e, c1, c2, p0, p1, p2, w, _grad_w)
    for index = 1:length(_data)
        x1 = _x1[index]
        x2 = _x2[index]

        tmp = w[NCLUST + 1]
        grad_w = zeros(NCLUST + 1)
        grad_w[NCLUST + 1] = 1.0

        for i = 1:NCLUST
            a = exp(p0[i])
            b = p1[i]
            c = exp(p2[i])

            tmp1 = (x1 - c1[i]);
            tmp2 = (x2 - c2[i]);
            dist = a * tmp1 * tmp1 - 2.0 * b * tmp1 * tmp2 + c * tmp2 * tmp2;
            act = exp(-dist);

            tmp += w[i] * act;
            grad_w[i] = act;
        end

        e = tmp - _data[index];
        _y[index] = tmp;
        _e[index] = e;

        #gradients
        for i = 1:NCLUST + 1
            _grad_w[i] += e * grad_w[i]
        end

        #println("index: $(index), x1: $(x1), x2: $(x2), y: $(tmp), e: $(e)")
    end
end

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

    grad_c1_buff = cl.Buffer(Float32, ctx, :w, length(grad_c1))
    grad_c2_buff = cl.Buffer(Float32, ctx, :w, length(grad_c2))
    grad_p0_buff = cl.Buffer(Float32, ctx, :w, length(grad_p0))
    grad_p1_buff = cl.Buffer(Float32, ctx, :w, length(grad_p1))
    grad_p2_buff = cl.Buffer(Float32, ctx, :w, length(grad_p2))
    grad_w_buff = cl.Buffer(Float32, ctx, :w, length(grad_w))

    #execute a forward pass
    @time queue(rbf_forward_pass, size(d), nothing, x1_buff, x2_buff, y_buff, data_buff, e_buff, c1_buff, c2_buff, p0_buff, p1_buff, p2_buff, w_buff, grad_w_buff)

    ocl_y = cl.read(queue, y_buff)
    ocl_e = cl.read(queue, e_buff)
    ocl_grad_w = cl.read(queue, grad_w_buff)        

    #validate in Julia
    @time rbf_forward_pass_julia(x1, x2, y, d, e, c1, c2, p0, p1, p2, w, grad_w)

    if isapprox(norm(y - ocl_y) / length(d), zero(Float32), atol = 1e-8)
        println("y: Success!")
    else
        println("y: Norm should be ≈ 0.0f, not ", norm(y - ocl_y) / length(d))
    end

    if isapprox(norm(grad_w - ocl_grad_w) / length(grad_w), zero(Float32), atol = 1e-8)
        println("grad_w: Success!")
    else
        println("grad_w: Norm should be ≈ 0.0f, not ", norm(grad_w - ocl_grad_w) / length(grad_w))
    end
end