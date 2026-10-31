#ifndef DATA_HPP
#define DATA_HPP
// # Data
// A synthetic signal to plot and summarize.

std::vector<double> samples;

for (int i = 0; i < 2000; ++i)
  samples.push_back(std::sin(i / 80.0) + 0.2 * ((i * 7919) % 13) / 13.0);

#endif
