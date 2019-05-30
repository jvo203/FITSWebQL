using LinearAlgebra
using Makie
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
        return (0, missing, missing, missing, missing, missing, missing, missing)
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

    if frame_min == frame_max
        return (frame_min, missing, missing, missing, missing, missing, missing, missing)
    end

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
    #NCLUST = min(XCLUST * YCLUST, count)
    NCLUST = max(1, min(Int(round(count / 64)), 1024))

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
        
        println(size(d))
        println(x1_buff, x2_buff)
        println(y_buff, data_buff, e_buff)

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
            
        println("\tGPU batch training iteration: $(iter)/$(NITER), error: ", norm(e))    
    
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
             
        scene = Scene(resolution = (1500, 1500))
        center!(scene)
    
        if count < capacity               
            @time queue(rbf_compute, capacity, nothing, x1test_buff, x2test_buff, ytest_buff, c1_buff, c2_buff, p0_buff, p1_buff, p2_buff, w_buff)
            y = cl.read(queue, ytest_buff)
            img = reshape(y, width, height)
            heatmap!(scene, img)
        else
            img = reshape(y, width, height)
            heatmap!(scene, img)
        end
    
        display(scene)
    end    

    return (NCLUST, missing, c1, c2, p0, p1, p2, w)
end