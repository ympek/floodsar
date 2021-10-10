#pragma once

#include <iostream>
#include "polarization.hpp"
#include "types.hpp"

class RasterInfo {
  public:
    RasterInfo(std::string filepath, Polarization pol_, Date date_) : absolutePath(filepath), pol(pol_), date(date_), proj4("") {}
    std::string absolutePath;
    std::string proj4;
    Polarization pol;
    Date date;
};
