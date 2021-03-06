#pragma once

#include <vector>
#include <iostream>
#include <cmath>
#include "types.hpp"

// I am not convinced it should really be returned by value ugh
std::vector<double> createSequence(double start, double end, double step) {
  std::vector<double> outputVector;

  double i = start;

  while(i <= end) {
    outputVector.push_back(i);
    i += step;
  }

  return outputVector;
}

// calculate Pearson correlation of two equally sized vectors
double calcCorrelationCoeff(
    std::vector<unsigned int>& floodedAreaVals,
    std::vector<double>& riverGaugeVals)
{
  auto numVals   = floodedAreaVals.size();
  auto numVals2  = riverGaugeVals.size();

  if (numVals != numVals2) {
    // to be transformed into actual runtime error
    std::cout << "ERROR: vectors not equal in size - " << numVals << " vs " << numVals2 << "\n";
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

  double corr = (numVals * sumXY - sumX * sumY)
    / std::sqrt((numVals * squareSumX - sumX * sumX) * (numVals * squareSumY - sumY * sumY));

  // std::cout << "corrcelel: " << corr << "\n";
  return corr;
}

Date hydrologicalToNormalDate(std::string_view year, std::string_view month, std::string_view day) {
  // we want to have format like 20190221
  // the dates have " " around them so strip them:
  // std::cout << year << month << day << std::endl;
  year.remove_prefix(1);  year.remove_suffix(1);
  month.remove_prefix(1); month.remove_suffix(1);
  day.remove_prefix(1);   day.remove_suffix(1);
  int numericYear  = std::stoi(static_cast<std::string>(year));
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

  std::string outMonth = realMonth < 10 ? "0" + std::to_string(realMonth) : std::to_string(realMonth);
  std::string outDay = numericDay < 10 ? "0" + std::to_string(numericDay) : std::to_string(numericDay);

  Date out {};
  out += std::to_string(realYear);
  out += outMonth;
  out += outDay;

  // std::cout << realYear << realMonth << numericDay << std::endl;

  return out;
}
