using CSV
using Plots

data4 = CSV.read("../../fits_web_ql/memory_usage.csv")
data5 = CSV.read("../memory_usage.csv")

timestamp4 = data4[:,1] ./ 1000
allocated4 = data4[:,2] ./ (1024 * 1024 * 1024)

timestamp5 = data5[:,1] ./ 1000
allocated5 = data5[:,2] ./ (1024 * 1024 * 1024)

common = min(size(timestamp4)[1], size(timestamp5)[1])

plot(timestamp5[1:common], [allocated4[1:common], allocated5[1:common]], label=["Rust fits_web_ql v4" "C/C++ FITSWebQL v5"], xlabel="elapsed time [s]", ylabel="jemalloc stats.allocated memory usage [GB]")
savefig("mem_two_way.pdf")