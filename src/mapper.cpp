#include <cxxopts.hpp>
#include "XYPair.hpp"
#include "gdal/cpl_conv.h" // for CPLMalloc()
#include "gdal/gdal_priv.h"
#include "gdal/ogrsf_frmts.h"
#include "rasters.hpp"
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

namespace fs = std::filesystem;

int
main(int argc, char** argv)
{
  GDALAllRegister();

  cxxopts::Options options("Floodsar::Mapper", " - command line options");

    options.add_options()("c,classes", "all classes", cxxopts::value<int>())(
    "f,floods", "flood classes", cxxopts::value<int>())(
    "b,base",
    "use base algo, provide polarization",
    cxxopts::value<std::string>())(
    "a,auto",
    "automatically use best k-means results");

  int numAllClassess = 2;
  int numFloodClasses = 1;
  int aa;

  std::vector<int> floodClasses;
  std::string pointsFile;
  std::string mapDirectory;
  std::ifstream datesFile(".floodsar-cache/dates.txt");
  std::vector<std::string> dates;
  std::string line;

  auto userInput = options.parse(argc, argv);

  if (datesFile.is_open()) {
    while (getline(datesFile, line)) {
      dates.push_back(line);
    }
    datesFile.close();
  } else
    std::cout << "Unable to open ./floodsar-cache/dates.txt";

  if (fs::exists("./raster")) fs::remove("./raster");
  std::cout << "copy as template: ./.floodsar-cache/cropped/resampled__VV_" + dates[0] << "\n";
  fs::copy_file("./.floodsar-cache/cropped/resampled__VV_" + dates[0], "./raster");
  
  //one may want to clean-up all, however, it is better ro remove only directories with conflict as now implemented
  //if (fs::exists("mapped")) fs::remove_all("mapped");
  

  if (userInput.count("base")) {
    // base algoritm mapping.
    floodClasses.push_back(1);
    std::string pol = userInput["base"].as<std::string>();
    // bedzie VV albo VH

    pointsFile = "./.floodsar-cache/1d_output/" + pol;
    mapDirectory = "./mapped/base_algo_pol_" + pol + "/";
  } else {
    // improved algo mapping.
    if (userInput.count("auto")) 
       {
        std::ifstream bestClassStream(".floodsar-cache/kmeans_outputs/best.txt");
        bestClassStream >> numAllClassess >> numFloodClasses;
        std::cout <<"Auto best classes\n k-means classes: " + std::to_string(numAllClassess) + ", Flood classes: " + std::to_string(numFloodClasses) +"\n";
       } else {
        numAllClassess = userInput["classes"].as<int>();
        numFloodClasses = userInput["floods"].as<int>();
       }

    std::string floodclassesFile =
      "./.floodsar-cache/kmeans_outputs/KMEANS_INPUT_cl_" +
      std::to_string(numAllClassess) + "/" + std::to_string(numAllClassess) +
      "-clusters.txt_" + std::to_string(numFloodClasses) + "_floodclasses.txt";

    std::ifstream floodclassesFileStream(floodclassesFile);

    int floodClass;

    while (floodclassesFileStream >> floodClass) {
      floodClasses.push_back(floodClass);
    }

    floodclassesFileStream.close();

    pointsFile = "./.floodsar-cache/kmeans_outputs/KMEANS_INPUT_cl_" +
                 std::to_string(numAllClassess) + "/" +
                 std::to_string(numAllClassess) + "-points.txt";

    mapDirectory = "./mapped/" + std::to_string(numAllClassess) + "__" +
                   std::to_string(numFloodClasses) + "/";
  }

  std::string rasterToClassify = "./raster";

  std::ifstream pointsStream(pointsFile);

  int point;
  int dateIndex = 0;
  int bufferIndex = 0;

  //remove only conflicts
  if (!fs::exists("mapped")) fs::create_directory("mapped");
  if (fs::exists(mapDirectory)) fs::remove_all(mapDirectory);
  fs::create_directory(mapDirectory);
  

  std::string mapPath = mapDirectory + dates[dateIndex] + ".tif";
  std::filesystem::copy_file(rasterToClassify, mapPath);

  int NoDataValue = -1;
  auto raster = static_cast<GDALDataset*>(GDALOpen(mapPath.c_str(), GA_Update));
  auto rasterBand = raster->GetRasterBand(1);
  unsigned int xSize = rasterBand->GetXSize();
  unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));

  int pointsExpected = xSize * ySize * dates.size();
  int pointsActual = 0;
  pointsActual += xSize * ySize;

  while (pointsStream >> point) {
    if (std::find(floodClasses.begin(), floodClasses.end(), point) !=
        floodClasses.end()) {
      buffer[bufferIndex] = 1;
    } else {
      buffer[bufferIndex] = 0;
    }
    bufferIndex++;

    if (bufferIndex == words) {
      bufferIndex = 0;

      auto error = rasterBand->RasterIO(
        GF_Write, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

      if (error == CE_Failure) {
        std::cout << "[] Could not read raster\n";
      } else {
        std::cout << "saved: " + mapPath + '\n';
      }

      auto noDataError = rasterBand->SetNoDataValue(NoDataValue);
      if(noDataError == CE_Failure) std::cout << "Could not set NoDataValue\n";

      raster->FlushCache();
      GDALClose(raster);

      dateIndex++;
      if (dateIndex >= dates.size()) {
        std::cout << "Its time to stop. \n";
        break;
      }
      mapPath = mapDirectory + dates[dateIndex] + ".tif";
      std::filesystem::copy_file(rasterToClassify, mapPath);

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
