#pragma once
#include "polarization.hpp"
#include "types.hpp"
#include <filesystem>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

/*
*
* Utility class for extracting infromation about satellite imagery
*
*/

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
/*
* 
* This class contains methods to retrevie acquistiion date and polarization for input satellite imagery filebuf*
*
*/
class RasterInfoExtractor
{
public:
  virtual RasterInfo extractFromPath(std::string filepath) = 0;
};

//Extractor for file preprocessed using Hyp3
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
//Extractor for locally processed imagery files (date + polarization) 
class StdExtractor : public RasterInfoExtractor
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

        std::string polToken = result[1].substr(0, 2);
        Polarization resultPol = stringToPol(polToken);
        std::string resultDate = result[0].substr(0, 8);

        // std::cout << "dateToken + pol = " << resultDate << "___"  <<
        // polToString(resultPol) << "\n";

        return RasterInfo(filepath, resultPol, resultDate);
    }
};
