#pragma once

#include <fstream>
#include <map>

#include "types.hpp"
#include "csv.hpp"

class HydroDataReader {
  public:
    HydroDataReader() {}

    void readFile(std::map<Date, double>& outputDataMap, std::string filePath)
    {
      std::ifstream file(filePath);

      CSVRow row;
      while(file >> row)
      {
        const Date day = static_cast<std::string>(row[0]);
        outputDataMap[day] = std::stod(static_cast<std::string>(row[1]));
      }
    }
};