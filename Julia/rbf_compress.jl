using NaNMath
using OpenCL
using PaddedViews
using Random
using Statistics

const NITER = 500

function rbf_compress_tile(tile, device, ctx, queue)
    dims = size(tile)
    println("\ttile dimensions: ", dims)

    padding = NaNMath.mean(tile)

    if isnan(padding)
        println("\tall values are NaN, skipping a tile")
        return true
    end

    #add extra padding if necessary
    #=
    if dims[1] < TILE_SIZE || dims[2] < TILE_SIZE        
        padding = mean(tile)
        println("\tpadding a tile with $(padding)")

        tile = PaddedView(padding, tile, (TILE_SIZE, TILE_SIZE))
        dims = size(tile)
        println("\tpadded tile dimensions: ", dims)
    end
    =#

    width = dims[1]
    height = dims[2]
    capacity = width * height
    
    println("\twidth : ", width, "\theight : ", height, "\tcapacity : ", capacity)

    (frame_min, frame_max) = NaNMath.extrema(tile)

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
            tmp = tile[ix,iy]
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

    (x1min, x1max) = NaNMath.extrema(x1)
    (x2min, x2max) = NaNMath.extrema(x2)

    println("count : $(count), $(x1min), $(x1max), $(x2min), $(x2max)")
    
    XCLUST = min(Int(round(width / 8)), 32)#/16
    YCLUST = min(Int(round(height / 8)), 32)#/16
    NCLUST = min(XCLUST * YCLUST, count)

    println("XCLUST : ", XCLUST, "\tYCLUST : ", YCLUST, "\tNCLUST : ", NCLUST)    

    compression_code = open("rbf.cl") do file    
        "#define NCLUST $(NCLUST)\n" * read(file, String)
    end
    
    program = cl.Program(ctx, source = compression_code) |> cl.build!
    rbf_gradient = cl.Kernel(program, "rbf_gradient")
    rbf_compute = cl.Kernel(program, "rbf_compute")

    #training data OK

    #parameter initialisation
    c1 = shuffle(x1)[1:NCLUST]
    c2 = shuffle(x2)[1:NCLUST]

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

    η₊ = 1.2
    η₋ = 0.5
    ΔMin = 1e-5
    ΔMax = 1e-1        
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

    return false
end