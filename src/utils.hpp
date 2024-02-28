#pragma once

#include "types.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

struct ClassifiedCentroid
{
  double vh;
  double vv;
  unsigned int cl;
};
//simple utilites to compare value of centroids by polariztion (vh,vv or sum).
struct compareByVH
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vh < c2.vh);
  }
};

struct compareByVV
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vv < c2.vv);
  }
};

struct compareBySum
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vv + c1.vh < c2.vv + c2.vh);
  }
};

std::string
getCurrentTimeString()
{
  auto t = std::chrono::system_clock::now();
  // TODO implement
  return "_";
}

#define LOG(msg, x...)                                                         \
  do {                                                                         \
    fprintf(stdout, "[%s] " msg "\n", getCurrentTimeString().c_str(), ##x);    \
    fflush(stdout);                                                            \
  } while (0)

// I am not convinced it should really be returned by value ugh
std::vector<double>
createSequence(double start, double end, double step)
{
  std::vector<double> outputVector;

  double i = start;

  while (i <= end) {
    outputVector.push_back(i);
    i += step;
  }

  return outputVector;
}

// calculate Pearson correlation of two equally sized vectors
double
calcCorrelationCoeff(std::vector<unsigned int>& floodedAreaVals,
                     std::vector<double>& riverGaugeVals)
{
  auto numVals = floodedAreaVals.size();
  auto numVals2 = riverGaugeVals.size();

  if (numVals != numVals2) {
    // to be transformed into actual runtime error
    std::cout << "ERROR: vectors not equal in size - " << numVals << " vs "
              << numVals2 << "\n";
  }

  double sumX, sumY, sumXY, squareSumX, squareSumY;
  sumX = sumY = sumXY = squareSumX = squareSumY = 0.0;

  for (unsigned int i = 0; i < numVals; i++) {
    double x = static_cast<double>(floodedAreaVals.at(i));
    double y = riverGaugeVals.at(i);
    sumX += x;
    sumY += y;
    sumXY += x * y;
    squareSumX += std::pow(x, 2);
    squareSumY += std::pow(y, 2);
  }

  double corr = (numVals * sumXY - sumX * sumY) /
                std::sqrt((numVals * squareSumX - sumX * sumX) *
                          (numVals * squareSumY - sumY * sumY));

  // std::cout << "corrcelel: " << corr << "\n";
  return corr;
}

//Converts hydrological date format to standard date format yyyymmdd
Date
hydrologicalToNormalDate(std::string_view year,
                         std::string_view month,
                         std::string_view day)
{
  // we want to have format like 20190221
  // the dates have " " around them so strip them:
  // std::cout << year << month << day << std::endl;
  year.remove_prefix(1);
  year.remove_suffix(1);
  month.remove_prefix(1);
  month.remove_suffix(1);
  day.remove_prefix(1);
  day.remove_suffix(1);
  int numericYear = std::stoi(static_cast<std::string>(year));
  int numericMonth = std::stoi(static_cast<std::string>(month));
  int numericDay = std::stoi(static_cast<std::string>(day));

  int realMonth = 0;
  int realYear = numericYear;

  bool decrementYear = false;
  if (numericMonth == 1) {
    realMonth = 11;
    decrementYear = true;
  } else if (numericMonth == 2) {
    realMonth = 12;
    decrementYear = true;
  } else {
    realMonth = numericMonth - 2;
  }

  if (decrementYear) {
    realYear--;
  }

  std::string outMonth = realMonth < 10 ? "0" + std::to_string(realMonth)
                                        : std::to_string(realMonth);
  std::string outDay = numericDay < 10 ? "0" + std::to_string(numericDay)
                                       : std::to_string(numericDay);

  Date out{};
  out += std::to_string(realYear);
  out += outMonth;
  out += outDay;

  // std::cout << realYear << realMonth << numericDay << std::endl;

  return out;
}

//utility function that prints obsElevationMaps to console
void
printMap(const std::map<std::string, double>& m)
{
  for (const auto& [key, value] : m) {
    std::cout << '[' << key << "] = " << value << "\n";
  }
  std::cout << '\n';
}

//creates neccasary local files 
void
createCacheDirectoryIfNotExists()
{
  fs::create_directory(".floodsar-cache");
  fs::create_directory(".floodsar-cache/cropped");
  fs::create_directory(".floodsar-cache/vrt");
  fs::create_directory(".floodsar-cache/reprojected");
  fs::create_directory(".floodsar-cache/proj4");
  fs::create_directory(".floodsar-cache/kmeans_inputs");
  fs::create_directory(".floodsar-cache/kmeans_outputs");
  fs::create_directory(".floodsar-cache/1d_output");
}