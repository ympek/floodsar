#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include "gdal_priv.h"
#include "cpl_conv.h" // for CPLMalloc()
#include "ogrsf_frmts.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <thread>
#include "XYPair.hpp"
#include "BoundingBox.hpp"
#include "utils.hpp"
#include <algorithm>
#include "../vendor/cxxopts.hpp"

namespace fs = std::filesystem;

//std::vector<std::string> dates {
//"20201101", "20201104", "20201105", "20201106", "20201107", "20201110", "20201111", "20201112", "20201113", "20201116", "20201117", "20201118", "20201119", "20201122", "20201123", "20201124", "20201125", "20201128", "20201129", "20201130", "20201201", "20201204", "20201205", "20201206", "20201207", "20201210", "20201211", "20201212", "20201213", "20201216", "20201217", "20201218", "20201219", "20201222", "20201223", "20201224", "20201225", "20201228", "20201229", "20201230", "20201231", "20210103", "20210104", "20210105", "20210106", "20210109", "20210110", "20210111", "20210112", "20210115", "20210116", "20210117", "20210118", "20210121", "20210122", "20210123", "20210124", "20210127", "20210128", "20210129", "20210202", "20210203", "20210204", "20210205", "20210208", "20210209", "20210210", "20210211", "20210214", "20210215", "20210216", "20210217", "20210220", "20210221", "20210222", "20210223", "20210226", "20210227", "20210228", "20210301", "20210304", "20210305", "20210306", "20210307", "20210310", "20210311", "20210312", "20210313", "20210316", "20210317", "20210318", "20210319", "20210322", "20210323", "20210324", "20210325", "20210328", "20210329", "20210330", "20210331" };

std::vector<std::string> dates {
"20170220", "20170222", "20170223", "20170225", "20170226", "20170228", "20170301", "20170303", "20170304", "20170306", "20170307", "20170309", "20170310", "20170312", "20170313", "20170315", "20170316", "20170318", "20170319", "20170321", "20170322", "20170324", "20170424", "20170524", "20170723", "DUMMY"
};

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

  auto userInput = options.parse(argc, argv);

  if(!fs::exists("./raster")) {
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
  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));

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
        std::cout << "zapisano " + mapPath + '\n';
      }

      dateIndex++;

      if (dateIndex > dates.size()) {
        std::cout << "Its time to stop. \n";
        break;
      }

      mapPath = mapDirectory + dates[dateIndex] + ".tif";
      std::filesystem::copy_file(rasterToClassify, mapPath);

      raster->FlushCache();
      GDALClose(raster);

      raster = static_cast<GDALDataset*>(GDALOpen(mapPath.c_str(), GA_Update));
      rasterBand = raster->GetRasterBand(1);
    }
  }

  pointsStream.close();

  CPLFree(buffer);

  return 0;
}
