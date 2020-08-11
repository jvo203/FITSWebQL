using CSV
using Plots

data = CSV.read("../memory_usage.csv")
println(data[1:5,:])