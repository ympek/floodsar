#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include "gdal/gdal_priv.h"
#include "gdal/cpl_conv.h" // for CPLMalloc()
#include "gdal/ogrsf_frmts.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include "XYPair.hpp"
#include "rasters.hpp"
#include "utils.hpp"
#include <algorithm>
#include "../vendor/cxxopts.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  GDALAllRegister();

  cxxopts::Options options("Floodsar::Mapper", " - command line options");

  options.add_options()
    ("c,classes", "all classes", cxxopts::value<int>())
    ("f,floods", "flood classes", cxxopts::value<int>())
    ("b,base", "use base algo, provide polarization", cxxopts::value<std::string>())
    ;

  int numAllClassess = 2;
  int numFloodClasses = 1;

  std::vector<int> floodClasses;
  std::string pointsFile;
  std::string mapDirectory;
  std::ifstream datesFile (".floodsar-cache/dates.txt");
  std::vector<std::string> dates;
  std::string line;

  auto userInput = options.parse(argc, argv);

  if (datesFile.is_open())
	{
		while ( getline (datesFile,line) )
		{
			dates.push_back(line);
		}
	datesFile.close();
	}
	else std::cout << "Unable to open ./floodsar-cache/dates.txt";

  if(!fs::exists("./raster")) {
	  std::cout << "./.floodsar-cache/cropped/resampled__VV_" + dates[0];
    fs::copy_file("./.floodsar-cache/cropped/resampled__VV_" + dates[0], "./raster");
  }

  if (userInput.count("base")) {
    // base algoritm mapping.
    floodClasses.push_back(1);
    std::string pol = userInput["base"].as<std::string>();
    // bedzie VV albo VH

    pointsFile = "./.floodsar-cache/1d_output/" + pol;
    mapDirectory = "./mapped/base_algo_pol_" + pol + "/";
  } else {
    // improved algo mapping.

    numAllClassess  = userInput["classes"].as<int>();
    numFloodClasses = userInput["floods"].as<int>();

    std::string floodclassesFile = "./.floodsar-cache/kmeans_outputs/KMEANS_INPUT_cl_"
      + std::to_string(numAllClassess) + "/" + std::to_string(numAllClassess)
      + "-clusters.txt_" + std::to_string(numFloodClasses) + "_floodclasses.txt";

    std::ifstream floodclassesFileStream(floodclassesFile);

    int floodClass;

    while (floodclassesFileStream >> floodClass) {
      floodClasses.push_back(floodClass);
    }

    floodclassesFileStream.close();

    pointsFile = "./.floodsar-cache/kmeans_outputs/KMEANS_INPUT_cl_"
      + std::to_string(numAllClassess) + "/" + std::to_string(numAllClassess)
      + "-points.txt";

    mapDirectory = "./mapped/" + std::to_string(numAllClassess) + "__" + std::to_string(numFloodClasses) + "/";
  }

  std::string rasterToClassify = "./raster";

  std::ifstream pointsStream(pointsFile);

  int point;
  int dateIndex = 0;
  int bufferIndex = 0;

  fs::create_directory("./mapped/");
  fs::create_directory(mapDirectory);

  std::string mapPath = mapDirectory + dates[dateIndex] + ".tif";
  std::filesystem::copy_file(rasterToClassify, mapPath);

  auto raster = static_cast<GDALDataset*>(GDALOpen(mapPath.c_str(), GA_Update));
  auto rasterBand = raster->GetRasterBand(1);
  unsigned int xSize = rasterBand->GetXSize();
  unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));

  int pointsExpected = xSize * ySize * dates.size();
  int pointsActual = 0;
  pointsActual += xSize * ySize;

  while(pointsStream >> point) {
    if (std::find(floodClasses.begin(), floodClasses.end(), point) != floodClasses.end()) {
      buffer[bufferIndex] = 1;
    } else {
      buffer[bufferIndex] = 0;
    }
    bufferIndex++;

    if (bufferIndex == words) {
      bufferIndex = 0;

      auto error = rasterBand->RasterIO(GF_Write, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

      if (error == CE_Failure) {
        std::cout << "[] Could not read raster\n";
      } else {
        std::cout << "saved: " + mapPath + '\n';
      }

      dateIndex++;
      if (dateIndex >= dates.size()) {
        std::cout << "Its time to stop. \n";
        break;
      }
      mapPath = mapDirectory + dates[dateIndex] + ".tif";
      std::filesystem::copy_file(rasterToClassify, mapPath);
      raster->FlushCache();
      GDALClose(raster);

      raster = static_cast<GDALDataset*>(GDALOpen(mapPath.c_str(), GA_Update));
      rasterBand = raster->GetRasterBand(1);
      xSize = rasterBand->GetXSize();
      ySize = rasterBand->GetYSize();
      pointsActual += xSize * ySize;
    }
  }

  pointsStream.close();

  CPLFree(buffer);

  return 0;
}
