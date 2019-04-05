using AstroImages
#] pkg> add https://github.com/JuliaAstro/AstroImages.jl

using FITSIO
using Plots
using Statistics
using Wavelets

dir = "/home/chris/ダウンロード"
file = "e20121211_0010500001_dp_sf_st_mos.fits"

fitspath = string(dir, "/", file)
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
range = @time extrema(data)
println("min,max:", range)
med = @time median(data)
println("median:", med)

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
