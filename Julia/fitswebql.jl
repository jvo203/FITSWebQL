using AstroImages
#] pkg> add https://github.com/JuliaAstro/AstroImages.jl

using FITSIO
using LinearAlgebra
using NaNMath; nm = NaNMath
using OpenCL
#using Plots
using Makie
using Random
using Statistics
using Wavelets

#pyplot()

dir = "/home/chris/ダウンロード"
#file = "e20121211_0010500001_dp_sf_st_mos.fits"
file = "ALMA01030862.fits"
#file = "ALMA01157085.fits"
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

function rbf_gradient_julia(_x1, _x2, _y, _data, _e, c1, c2, p0, p1, p2, w, _grad_c1, _grad_c2, _grad_p0, _grad_p1, _grad_p2, _grad_w)
    for index = 1:length(_data)
        x1 = _x1[index]
        x2 = _x2[index]

        tmp = w[NCLUST + 1]
        grad_c1 = zeros(NCLUST)
        grad_c2 = zeros(NCLUST)
        grad_p0 = zeros(NCLUST)
        grad_p1 = zeros(NCLUST)
        grad_p2 = zeros(NCLUST)
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

            #gradients
            grad_w[i] = act;
            grad_c1[i] = 2.0 * w[i] * act * (a * tmp1 - b * tmp2)
           	grad_c2[i] = 2.0 * w[i] * act * (c * tmp2 - b * tmp1)
           	grad_p0[i] = w[i] * act * a * ( - tmp1 * tmp1 )
           	grad_p1[i] = 2.0 * w[i] * act * tmp1 * tmp2
           	grad_p2[i] = w[i] * act * c * ( -tmp2 * tmp2)
        end

        e = tmp - _data[index];
        _y[index] = tmp;
        _e[index] = e;

        #gradients
        for i = 1:NCLUST + 1
            _grad_w[i] += e * grad_w[i]
        end

        for i = 1:NCLUST
            _grad_c1[i] += e * grad_c1[i]
            _grad_c2[i] += e * grad_c2[i]
            _grad_p0[i] += e * grad_p0[i]
            _grad_p1[i] += e * grad_p1[i]
            _grad_p2[i] += e * grad_p2[i]
        end

        #println("index: $(index), x1: $(x1), x2: $(x2), y: $(tmp), e: $(e)")
    end
end

dims = size(data)
width = dims[1]
height = dims[2]
depth = dims[3]
capacity = width * height

XCLUST = min(Int(round(width / 8)), 32)#/16
YCLUST = min(Int(round(height / 8)), 32)#/16
NCLUST = XCLUST * YCLUST
NITER = 500

println("width : ", width, "\theight : ", height, "\tdepth : ", depth, "\tcapacity : ", capacity)
println("XCLUST : ", XCLUST, "\tYCLUST : ", YCLUST, "\tNCLUST : ", NCLUST)

device, ctx, queue = cl.create_compute_context()
println(device)

compression_code = open("rbf.cl") do file
    #define NCLUST dynamically
    "#define NCLUST $(NCLUST)\n" * read(file, String)
end

program = cl.Program(ctx, source = compression_code) |> cl.build!
rbf_gradient = cl.Kernel(program, "rbf_gradient")
rbf_compute = cl.Kernel(program, "rbf_compute")

#scene = Scene(resolution = (500, 500))
#center!(scene)

for frame = Int(round(depth / 2)):Int(round(depth / 2))#1:depth
#for frame = 1:5
    sub = view(data, :, :, frame, 1)
    println("frame : ", frame, "\tdims: ", size(sub))
    (frame_min, frame_max) = @time nm.extrema(sub)

    x1 = Float32[]
    x2 = Float32[]
    d = Float32[]
    y = Float32[]
    e = Float32[]
    count = 0

    x1test = Float32[]
    x2test = Float32[]    

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
            push!(x1test, (ix - 1) / (width - 1))   #[0,1]
            push!(x2test, (iy - 1) / (height - 1))  #[0,1]
        end
    end

    (x1min, x1max) = nm.extrema(x1)
    (x2min, x2max) = nm.extrema(x2)

    println("count : $(count), $(x1min), $(x1max), $(x2min), $(x2max)")
    
    #training data OK    

    #parameter initialisation
    c1 = shuffle(x1)[1:NCLUST]
    c2 = shuffle(x2)[1:NCLUST]  
    
    #=
    c1 = Float32[]
    c2 = Float32[]

    for i = 1:XCLUST
        for j = 1:YCLUST
            push!(c1, (i - 1) / (XCLUST - 1))
            push!(c2, (j - 1) / (YCLUST - 1))
        end
    end
    =#

    scene = Scene(resolution = (1500, 1500))
    center!(scene)
    scatter!(scene, c1, c2, markersize = 1 / NCLUST)
    display(scene)
    #gui()

    σX = 0.1 / (XCLUST - 1)
    σY = 0.1 / (YCLUST - 1)
    σX = randn(NCLUST) * (0.01 * σX) .+ σX
    σY = randn(NCLUST) * (0.01 * σY) .+ σY
    θ = 2 * π * rand(NCLUST)    

    a = 0.5 * cos.(θ) .* cos.(θ) ./ (σX .* σX) .+ 0.5 * sin.(θ) .* sin.(θ) ./ (σY .* σY)
    b = -0.25 * sin.(2.0 * θ) ./ (σX .* σX) .+ 0.25 * sin.(2.0 * θ) ./ (σY .* σY)
    c = 0.5 * sin.(θ) .* sin.(θ) ./ (σX .* σX) .+ 0.5 * cos.(θ) .* cos.(θ) ./ (σY .* σY)
    
    p0 = log.(a)
    p1 = b
    p2 = log.(c)

    w = randn(Float32, NCLUST + 1)

    #learning rates etc.
    η₊ = 1.2
    η₋ = 0.5
    ΔMin = 1e-5
    ΔMax = 1e-1
    #ΔMin = 1e-7#1e-5
    #ΔMax = 1e-3#1e-1
    d₀ = ΔMin

    #previous gradients
    grad_c1_prev = zeros(Float32, NCLUST)
    grad_c2_prev = zeros(Float32, NCLUST)
    grad_p0_prev = zeros(Float32, NCLUST)
    grad_p1_prev = zeros(Float32, NCLUST)
    grad_p2_prev = zeros(Float32, NCLUST)
    grad_w_prev = zeros(Float32, NCLUST + 1)

    #changes
    Δc1 = fill(Float32(d₀), NCLUST)
    Δc2 = fill(Float32(d₀), NCLUST)
    Δp0 = fill(Float32(d₀), NCLUST)
    Δp1 = fill(Float32(d₀), NCLUST)
    Δp2 = fill(Float32(d₀), NCLUST)
    Δw = fill(Float32(d₀), NCLUST + 1)

    #OpenCL buffers
    data_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = d)
    x1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x1)
    x2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x2)
    y_buff = cl.Buffer(Float32, ctx, :w, length(y))
    e_buff = cl.Buffer(Float32, ctx, :w, length(e))        

    x1test_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x1test)
    x2test_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = x2test)
    ytest_buff = cl.Buffer(Float32, ctx, :w, length(x1test))

    #println("w (before):", w)

    for iter = 1:NITER
    #gradients
        grad_c1 = zeros(Float32, NCLUST)
        grad_c2 = zeros(Float32, NCLUST)
        grad_p0 = zeros(Float32, NCLUST)
        grad_p1 = zeros(Float32, NCLUST)
        grad_p2 = zeros(Float32, NCLUST)
        grad_w = zeros(Float32, NCLUST + 1)

    #gradient buffers
    #=
        grad_c1_buff = cl.Buffer(Float32, ctx, :w, length(grad_c1))
        grad_c2_buff = cl.Buffer(Float32, ctx, :w, length(grad_c2))
        grad_p0_buff = cl.Buffer(Float32, ctx, :w, length(grad_p0))
        grad_p1_buff = cl.Buffer(Float32, ctx, :w, length(grad_p1))
        grad_p2_buff = cl.Buffer(Float32, ctx, :w, length(grad_p2))
        grad_w_buff = cl.Buffer(Float32, ctx, :w, length(grad_w))
    =#
        grad_c1_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_c1)
        grad_c2_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_c2)
        grad_p0_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_p0)
        grad_p1_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_p1)
        grad_p2_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_p2)
        grad_w_buff = cl.Buffer(Float32, ctx, (:w, :copy), hostbuf = grad_w)
            
    #parameter buffers    
        c1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = c1)
        c2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = c2)
        p0_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p0))
        p1_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p1))
        p2_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = Float32.(p2))
        w_buff = cl.Buffer(Float32, ctx, (:r, :copy), hostbuf = w)
    
    #execute a forward pass    
        @time queue(rbf_gradient, size(d), nothing, x1_buff, x2_buff, y_buff, data_buff, e_buff, c1_buff, c2_buff, p0_buff, p1_buff, p2_buff, w_buff, grad_c1_buff, grad_c2_buff, grad_p0_buff, grad_p1_buff, grad_p2_buff, grad_w_buff)

        y = cl.read(queue, y_buff)
        e = cl.read(queue, e_buff)
        grad_c1 = cl.read(queue, grad_c1_buff)
        grad_c2 = cl.read(queue, grad_c2_buff)
        grad_p0 = cl.read(queue, grad_p0_buff)
        grad_p1 = cl.read(queue, grad_p1_buff)
        grad_p2 = cl.read(queue, grad_p2_buff)
        grad_w = cl.read(queue, grad_w_buff)
        
        println("frame $(frame) ==> GPU batch training iteration: $(iter), error: ", norm(e))         

        #@time rbf_gradient_julia(x1, x2, y, d, e, c1, c2, p0, p1, p2, w, grad_c1, grad_c2, grad_p0, grad_p1, grad_p2, grad_w)
        #println("frame $(frame) ==> CPU batch training iteration: $(iter), error: ", norm(e))

    #update parameters
        #w
        for i = 1:NCLUST + 1
            if grad_w_prev[i] * grad_w[i] > 0.0
                Δw[i] = min(ΔMax, η₊ * Δw[i])
            end
        
            if grad_w_prev[i] * grad_w[i] < 0.0                
                Δw[i] = max(ΔMin, η₋ * Δw[i])
                grad_w[i] = 0.0
            end
                                
            w[i] = w[i] - sign(grad_w[i]) * Δw[i]                                  
            grad_w_prev[i] = grad_w[i] ;
        end
        
        #c1, c2, p0, p1, p2
        for i = 1:NCLUST
            #c1
            if grad_c1_prev[i] * grad_c1[i] > 0.0
                Δc1[i] = min(ΔMax, η₊ * Δc1[i])
            end
        
            if grad_c1_prev[i] * grad_c1[i] < 0.0                
                Δc1[i] = max(ΔMin, η₋ * Δc1[i])
                grad_c1[i] = 0.0
            end
                                
            c1[i] = c1[i] - sign(grad_c1[i]) * Δc1[i]  
            grad_c1_prev[i] = grad_c1[i] ;

            #c2
            if grad_c2_prev[i] * grad_c2[i] > 0.0
                Δc2[i] = min(ΔMax, η₊ * Δc2[i])
            end
        
            if grad_c2_prev[i] * grad_c2[i] < 0.0                
                Δc2[i] = max(ΔMin, η₋ * Δc2[i])
                grad_c2[i] = 0.0
            end
                                
            c2[i] = c2[i] - sign(grad_c2[i]) * Δc2[i]  
            grad_c2_prev[i] = grad_c2[i] ;

            #p0
            if grad_p0_prev[i] * grad_p0[i] > 0.0
                Δp0[i] = min(ΔMax, η₊ * Δp0[i])
            end
        
            if grad_p0_prev[i] * grad_p0[i] < 0.0                
                Δp0[i] = max(ΔMin, η₋ * Δp0[i])
                grad_p0[i] = 0.0
            end
                                
            p0[i] = p0[i] - sign(grad_p0[i]) * Δp0[i]  
            grad_p0_prev[i] = grad_p0[i] ;

            #p1
            if grad_p1_prev[i] * grad_p1[i] > 0.0
                Δp1[i] = min(ΔMax, η₊ * Δp1[i])
            end
        
            if grad_p1_prev[i] * grad_p1[i] < 0.0                
                Δp1[i] = max(ΔMin, η₋ * Δp1[i])
                grad_p1[i] = 0.0
            end
                                
            p1[i] = p1[i] - sign(grad_p1[i]) * Δp1[i]  
            grad_p1_prev[i] = grad_p1[i] ;

            #p2
            if grad_p2_prev[i] * grad_p2[i] > 0.0
                Δp2[i] = min(ΔMax, η₊ * Δp2[i])
            end
        
            if grad_p2_prev[i] * grad_p2[i] < 0.0                
                Δp2[i] = max(ΔMin, η₋ * Δp2[i])
                grad_p2[i] = 0.0
            end
                                
            p2[i] = p2[i] - sign(grad_p2[i]) * Δp2[i]  
            grad_p2_prev[i] = grad_p2[i] ;
        end

        #plot(x1, x2, ocl_y)
        #density(x1, x2, ocl_y)
        #scatter(c1, c2)
        #gui()

        #scene = Scene(resolution = (500, 500))
        #center!(scene)
        #scatter!(scene, c1, c2, markersize = 1 / NCLUST)                
        #AbstractPlotting.force_update!()
        scene = Scene(resolution = (1500, 1500))
        center!(scene)

        if count < capacity          
            #scatter!(scene, c1, c2, markersize = 1 / NCLUST)

            @time queue(rbf_compute, capacity, nothing, x1test_buff, x2test_buff, ytest_buff, c1_buff, c2_buff, p0_buff, p1_buff, p2_buff, w_buff)
            y = cl.read(queue, ytest_buff)
            img = reshape(y, width, height)
            heatmap!(scene, img)
        else
            img = reshape(y, width, height)
            heatmap!(scene, img)
        end

        display(scene)

        #scatter!(scene, x1, x2, ocl_y, markersize = 1 / NCLUST)    

    #validate in Julia
    #=
        @time rbf_gradient_julia(x1, x2, y, d, e, c1, c2, p0, p1, p2, w, grad_c1, grad_c2, grad_p0, grad_p1, grad_p2, grad_w)

        if isapprox(norm(y - ocl_y) / length(d), zero(Float32), atol = 1e-8)
            println("y: Success!")
        else
            println("y: Norm should be ≈ 0.0f, not ", norm(y - ocl_y) / length(d))
        end

        if isapprox(norm(grad_c1 - ocl_grad_c1) / length(grad_c1), zero(Float32), atol = 1e-8)
            println("grad_c1: Success!")
        else
            println("grad_c1: Norm should be ≈ 0.0f, not ", norm(grad_c1 - ocl_grad_c1) / length(grad_c1))
        end

        if isapprox(norm(grad_c2 - ocl_grad_c2) / length(grad_c2), zero(Float32), atol = 1e-8)
            println("grad_c2: Success!")
        else
            println("grad_c2: Norm should be ≈ 0.0f, not ", norm(grad_c2 - ocl_grad_c2) / length(grad_c2))
        end

        if isapprox(norm(grad_p0 - ocl_grad_p0) / length(grad_p0), zero(Float32), atol = 1e-8)
            println("grad_p0: Success!")
        else
            println("grad_p0: Norm should be ≈ 0.0f, not ", norm(grad_p0 - ocl_grad_p0) / length(grad_p0))
        end

        if isapprox(norm(grad_p1 - ocl_grad_p1) / length(grad_p1), zero(Float32), atol = 1e-8)
            println("grad_p1: Success!")
        else
            println("grad_p1: Norm should be ≈ 0.0f, not ", norm(grad_p1 - ocl_grad_p1) / length(grad_p1))
        end

        if isapprox(norm(grad_p2 - ocl_grad_p2) / length(grad_p2), zero(Float32), atol = 1e-8)
            println("grad_p2: Success!")
        else
            println("grad_p2: Norm should be ≈ 0.0f, not ", norm(grad_p2 - ocl_grad_p2) / length(grad_p2))
        end

        if isapprox(norm(grad_w - ocl_grad_w) / length(grad_w), zero(Float32), atol = 1e-8)
            println("grad_w: Success!")
        else
            println("grad_w: Norm should be ≈ 0.0f, not ", norm(grad_w - ocl_grad_w) / length(grad_w))
        end
    =#
    end

    #println("w (after):", w)
    #println("θ (after):", θ)
end