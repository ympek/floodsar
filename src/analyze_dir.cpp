#include "../vendor/cxxopts.hpp"
#include "BoundingBox.hpp"
#include "XYPair.hpp"
#include "gdal/cpl_conv.h" // for CPLMalloc()
#include "gdal/gdal_priv.h"
#include "gdal/ogrsf_frmts.h"
#include "utils.hpp"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>

/*
* Utility function that contains main() . This script can is used additionaly to analyze and prepare input data for the floodsar 
* algorithm.
*/

namespace fs = std::filesystem;

void
println(std::string message)
{
  std::cout << message << "\n";
}

void
die(std::string message)
{
  std::cout << "message"
            << "\n";
  exit(1);
}

void
getProjectionInfo(std::string rasterPath, std::string proj4filePath)
{
  std::string command = "gdalsrsinfo";

  command.append(" --single-line -o epsg ");
  command.append(rasterPath);

  std::system(command.c_str());
}

int
main(int argc, char** argv)
{
  GDALAllRegister();

  cxxopts::Options options("Floodsar::AnalyzeDir", " yes");
  options.add_options()(
    "d,dir", "directory to check", cxxopts::value<std::string>());
  options.parse_positional({ "dir" });

  auto userInput = options.parse(argc, argv);
  auto dir = userInput["dir"].as<std::string>();

  if (!fs::exists(dir)) {
    die("No such directory: " + dir);
  }

  println("checking " + dir);

  for (auto& p : fs::recursive_directory_iterator(dir)) {
    auto filepath = p.path();
    std::string proj4Path =
      ".floodsar-cache/proj4/" + filepath.filename().string() + "_proj4";
    std::cout << "proj4Path " << proj4Path << "\n";
    // if (!fs::exists(proj4Path)) {
    std::cout << "Getting projection info for " + filepath.string();
    getProjectionInfo(filepath.string(), proj4Path);
    //}

    std::ifstream proj4file(proj4Path);

	//get polarization and date info from imagery filenames
    RasterInfo extractedInfo = extractor->extractFromPath(filepath.string());

    std::getline(proj4file, extractedInfo.proj4);
    std::cout << polToString(extractedInfo.pol) + "\n";
    std::cout << extractedInfo.date + "\n";
    std::cout << extractedInfo.proj4 + "\n";
    std::cout << "\n";
    infos.push_back(extractedInfo);
  }
  return 0;
}
