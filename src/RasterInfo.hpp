#pragma once
#include "polarization.hpp"
#include "types.hpp"
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

class RasterInfo
{
public:
  RasterInfo(std::string filepath, Polarization pol_, Date date_)
    : absolutePath(filepath)
    , pol(pol_)
    , date(date_)
    , proj4("")
  {
  }
  std::string absolutePath;
  std::string proj4;
  Polarization pol;
  Date date;
};

class RasterInfoExtractor
{
public:
  virtual RasterInfo extractFromPath(std::string filepath) = 0;
};

class AsfExtractor : public RasterInfoExtractor
{
public:
  RasterInfo extractFromPath(std::string filepath)
  {
    std::vector<std::string> result;
    std::stringstream data(fs::path(filepath).filename());
    std::string line;

    while (std::getline(data, line, '_')) {
      result.push_back(line);
    }

    std::string polToken = result[8].substr(0, 2);
    Polarization resultPol = stringToPol(polToken);
    std::string resultDate = result[2].substr(0, 8);

    // std::cout << "dateToken + pol = " << resultDate << "___"  <<
    // polToString(resultPol) << "\n";

    return RasterInfo(filepath, resultPol, resultDate);
  }
};
