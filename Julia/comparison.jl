using CSV
using Plots

data4 = CSV.read("../../fits_web_ql/memory_usage.csv")
data5 = CSV.read("../memory_usage.csv")

timestamp4 = data4[:,1] ./ 1000
allocated4 = data4[:,2] ./ (1024 * 1024)

timestamp5 = data5[:,1] ./ 1000
allocated5 = data5[:,2] ./ (1024 * 1024)

plot(timestamp4, [allocated4, allocated5], label=["Rust fits_web_ql v4" "C/C++ FITSWebQL SE v5"], xlabel="elapsed time [s]", ylabel="jemalloc stats.allocated memory usage [MB]")
savefig("mem_two_way.pdf")