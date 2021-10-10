#pragma once

#include <iostream>

enum class Polarization {
  VH,
  VV
};

std::string polToString(Polarization pol) {
  if (pol == Polarization::VV) {
    return "VV";
  } else if (pol == Polarization::VH) {
    return "VH";
  } else {
    return "ERROR";
  }
}

Polarization stringToPol(std::string str) {
  Polarization pol;
  if (str == "VH") {
    pol = Polarization::VH;
  }
  else if (str == "VV") {
    pol = Polarization::VV;
  } else {
    std::cout << "Sth wrong\n";
  }
  return pol;
}
