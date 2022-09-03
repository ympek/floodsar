#pragma once
#include <filesystem>
#include <iostream>
#include "polarization.hpp"
#include "types.hpp"
#include <vector>

namespace fs = std::filesystem;

class RasterInfo {
  public:
    RasterInfo(std::string filepath, Polarization pol_, Date date_) : absolutePath(filepath), pol(pol_), date(date_), proj4("") {}
    std::string absolutePath;
    std::string proj4;
    Polarization pol;
    Date date;
};

class RasterInfoExtractor {
  public:
    virtual RasterInfo extractFromPath(std::string filepath) = 0;
};

class MinimalExampleExtractor : public RasterInfoExtractor {
  public:
    RasterInfo extractFromPath(std::string filepath)
    {
      Polarization pol;
      std::string subPol = filepath.substr(13, 2);
      std::string subDat = filepath.substr(16, 8);
      pol = stringToPol(subPol);
      return RasterInfo(filepath, pol, subDat);
    }
};

class HypeExtractor : public RasterInfoExtractor {
  public:
    RasterInfo extractFromPath(std::string filepath)
    {
      std::vector<std::string> result;

      std::stringstream data(filepath);

      std::string line;

      while(std::getline(data, line, '_'))
      {
        result.push_back(line); // Note: You may get a couple of blank lines
      }
      std::string polToken = result[result.size() - 1];
      Polarization resultPol;

      std::string::size_type loc = polToken.find("VV", 0);
      if( loc != std::string::npos ) {
        resultPol = Polarization::VV;
      }
      else {
        resultPol = Polarization::VH;
      }

      std::string dateToken = result[result.size() - 3];

      return RasterInfo(filepath, resultPol, dateToken.substr(0, 8));
    }
};

class AsfExtractor : public RasterInfoExtractor {
  public:
    RasterInfo extractFromPath(std::string filepath)
    {
      std::vector<std::string> result;

      std::stringstream data(fs::path(filepath).filename());

      std::string line;

      while(std::getline(data, line, '_'))
      {
        result.push_back(line); // Note: You may get a couple of blank lines
      }
      std::string polToken = result[result.size() - 1];
      Polarization resultPol;

      std::string::size_type loc = polToken.find("VV", 0);
      if( loc != std::string::npos ) {
        resultPol = Polarization::VV;
      }
      else {
        resultPol = Polarization::VH;
      }

      std::string dateToken = result[2];

      std::cout << "dateToken + pol = " << dateToken << "___"  << polToString(resultPol) << "\n";

      return RasterInfo(filepath, resultPol, dateToken.substr(0, 8));
    }
};

class DebugExtractor : public RasterInfoExtractor {
  public:
    RasterInfo extractFromPath(std::string filepath)
    {
      std::vector<std::string> result;
      std::stringstream data(fs::path(filepath).filename());
      std::string line;

      while(std::getline(data, line, '_')) {
        result.push_back(line);
      }

      std::string polToken = result[8].substr(0, 2);
      Polarization resultPol = stringToPol(polToken);
      std::string resultDate = result[2].substr(0, 8);

      std::cout << "dateToken + pol = " << resultDate << "___"  << polToString(resultPol) << "\n";

      return RasterInfo(filepath, resultPol, resultDate);
    }
};
