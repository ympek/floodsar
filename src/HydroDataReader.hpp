#pragma once

#include <fstream>
#include <map>

#include "csv.hpp"
#include "types.hpp"

/*
Class for paring discharge data.
*/

class HydroDataReader
{
public:
  HydroDataReader() {}

  void readFile(std::map<Date, double>& outputDataMap, std::string filePath)
  {
    std::ifstream file(filePath);

    CSVRow row;
    while (file >> row) {
      const Date day = static_cast<std::string>(row[0]);
      outputDataMap[day] = std::stod(static_cast<std::string>(row[1]));
    }
  }
};