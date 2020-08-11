using CSV
using Plots

data = CSV.read("../memory_usage.csv")
println(data[1:5,:])

timestamp = data[:,1] ./ (1024 * 1024)
allocated = data[:,2] ./ (1024 * 1024)

plot(timestamp, allocated, label="jemalloc stats.allocated memory usage [MB]", xlabel="time [ms]", ylabel="memory [MB]")
savefig("mem_allocated_v5.pdf")