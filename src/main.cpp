#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include "gdal_priv.h"
#include "cpl_conv.h" // for CPLMalloc()
#include "ogrsf_frmts.h"
#include <fcntl.h>

#include "../vendor/cxxopts.hpp"

#include "types.hpp"
#include "utils.hpp"
#include "csv.hpp"
#include "polarization.hpp"
#include "rasters.hpp"
#include "RasterInfo.hpp"
#include "HydroDataReader.hpp"

namespace fs = std::filesystem;

const std::string kmeansInputFilename = "KMEANS_INPUT";

void performClusteringViaKMeansBinary(std::string inputFilename, int numClasses) {
  std::string outDir = ".floodsar-cache/kmeans_outputs/" + inputFilename + "_cl_" + std::to_string(numClasses);

  fs::create_directory(outDir);

  std::string command = "./bin/kmeans";
  command.append(" ");
  command.append("./.floodsar-cache/kmeans_inputs/" + inputFilename);
  command.append(" ");
  command.append(std::to_string(numClasses));
  command.append(" ");
  command.append(outDir);

  std::system(command.c_str());
}

void writeThresholdingResultsToFile(GDALDataset* raster, double threshold, std::ofstream& ofs) {
  auto rasterBand = raster->GetRasterBand(1);
  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

  if (error == CE_Failure) {
    std::cout << "Could not read raster";
  }

  for (int i = 0; i < words; i++) {
    if (buffer[i] < threshold) {
      ofs << "1" << "\n";
    } else {
      ofs << "0" << "\n";
    }
  }

  CPLFree(buffer);
}

unsigned int calcFloodedArea(GDALDataset* raster, double threshold) {
  auto rasterBand = raster->GetRasterBand(1);
  unsigned int floodedArea = 0; // sum of flooded pixels according to threshold;
  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

  if (error == CE_Failure) {
    std::cout << "Could not read raster";
    return 0;
  }

  for (int i = 0; i < words; i++) {
    if (buffer[i] < threshold) {
      ++floodedArea;
    }
  }

  CPLFree(buffer);

  return floodedArea;
}

void getProjectionInfo(std::string rasterPath, std::string proj4filePath) {
  std::string command = "gdalsrsinfo";

  command.append(" --single-line -o proj4 ");
  command.append(rasterPath);
  command.append(" >" + proj4filePath);

  std::system(command.c_str());
}

// returns absolute paths to all images that will take part in calculating the result
std::vector<RasterInfo> readRasterDirectory(std::string dirname, std::string fileExtension, RasterInfoExtractor* extractor)
{
  std::vector<RasterInfo> infos;

  for(auto& p: fs::recursive_directory_iterator(dirname)) {
    auto filepath = p.path();
    if (filepath.extension().string() == fileExtension) {
      std::string proj4Path = ".floodsar-cache/proj4/" + filepath.filename().string() + "_proj4";
      std::cout << "proj4Path " << proj4Path << "\n";
      if (!fs::exists(proj4Path)) {
        std::cout << "Getting projection info for " + filepath.string();
        getProjectionInfo(filepath.string(), proj4Path);
      }
      std::ifstream proj4file(proj4Path);

      RasterInfo extractedInfo = extractor->extractFromPath(filepath.string());
      std::getline(proj4file, extractedInfo.proj4);
      infos.push_back(extractedInfo);
    }
  }

  return infos;
}

void mosaicRasters(const std::string targetPath, const std::vector<std::string>& rasterList) {
  std::string command = "gdalbuildvrt";
  command.append(" ");
  command.append(targetPath);
  command.append(" ");

  for (int i = 0; i < rasterList.size(); i++) {
    command.append(rasterList[i]);
    command.append(" ");
  }
  std::system(command.c_str());
}

void performMosaicking(std::vector<RasterInfo>& rasterInfos, std::vector<RasterInfo>& outputVector) {
  std::map<std::string, std::vector<RasterInfo>> rastersMap;

  std::vector<std::thread> threads;

  for (int i = 0; i < rasterInfos.size(); i++) {
    auto date = rasterInfos[i].date;
    auto pol  = rasterInfos[i].pol;

    auto key = polToString(pol) + "_" + date;

    rastersMap[key].push_back(rasterInfos[i]);
  }

  for (const auto& [key, rasters] : rastersMap) {
    if (rasters.size() == 1) {
      // there is only one raster from this day, no worries.
      outputVector.push_back(rasters.at(0));
    } else if (rasters.size() > 0) {
      std::vector<std::string> pathsOnly;
      for (auto& info : rasters) {
        pathsOnly.push_back(info.absolutePath);
      }

      std::string targetPath = "./.floodsar-cache/vrt/" + key + ".vrt";

      // there is more, probably two. Need to mosaic them.
      threads.push_back(std::thread(mosaicRasters, targetPath, pathsOnly));
      outputVector.push_back({ targetPath, rasters.at(0).pol, rasters.at(0).date });
    }
  }

  for (auto &th : threads) {
    th.join();
  }
}

void createCacheDirectoryIfNotExists() {
  fs::create_directory(".floodsar-cache");
  fs::create_directory(".floodsar-cache/cropped");
  fs::create_directory(".floodsar-cache/vrt");
  fs::create_directory(".floodsar-cache/reprojected");
  fs::create_directory(".floodsar-cache/proj4");
  fs::create_directory(".floodsar-cache/kmeans_inputs");
  fs::create_directory(".floodsar-cache/kmeans_outputs");
  fs::create_directory(".floodsar-cache/1d_output");
}

std::string performReprojection(RasterInfo& info) {
  std::string command = "gdalwarp";
  std::string filename = ".floodsar-cache/reprojected/repd_" + polToString(info.pol) + "_" + info.date;

  if (fs::exists(filename)) {
    std::cout << "skipping reprojection for " << info.date << "_" << polToString(info.pol) << ". File is there...";
    return filename;
  }

  command.append(" -t_srs EPSG:32634 -overwrite ");
  command.append(info.absolutePath);
  command.append(" ");
  command.append(filename);

  std::system(command.c_str());

  return filename;
}

void reprojectIfNeeded(std::vector<RasterInfo>& rasters) {
  for (auto& it : rasters) {
    std::cout << "checking if reprojection needed " << it.proj4;
    // FIXME: hardcoded values.
    if ("+proj=utm +zone=35 +datum=WGS84 +units=m +no_defs" == it.proj4) {
      // needs reprojection.
      auto newPath = performReprojection(it);
      std::cout << "reprojection for " << it.absolutePath << " now is " << newPath;
      it.absolutePath = newPath;
    }
  }
}

class PairedImage {
  public:
    std::string pathVH;
    std::string pathVV;

    PairedImage(std::string pathVH_, std::string pathVV_) : pathVH(pathVH_), pathVV(pathVV_) {};
};

void getPixelValuesFromRaster(GDALDataset* raster, std::vector<double>& pixelValuesVector) {
  auto rasterBand = raster->GetRasterBand(1);

  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

  if (error == CE_Failure) {
    std::cout << "[getPixelValuesFromRaster] Could not read raster\n";
  } else {
    for (int i = 0; i < words; i++) {
      pixelValuesVector.push_back(buffer[i]);
    }
  }

  CPLFree(buffer);
}

struct ClassifiedCentroid {
  double vh;
  double vv;
  unsigned int cl;
};

struct compareByVH {
  inline bool operator() (const ClassifiedCentroid& c1, const ClassifiedCentroid& c2) {
    return (c1.vh < c2.vh);
  }
};

struct compareByVV {
  inline bool operator() (const ClassifiedCentroid& c1, const ClassifiedCentroid& c2) {
    return (c1.vv < c2.vv);
  }
};

struct compareBySum {
  inline bool operator() (const ClassifiedCentroid& c1, const ClassifiedCentroid& c2) {
    return (c1.vv + c1.vh < c2.vv + c2.vh);
  }
};

std::vector<unsigned int> createFloodClassesList(std::string clustersFilePath, unsigned int classesNum, std::string strategy) {
  unsigned int index = 1;
  std::ifstream infile(clustersFilePath);
  double centerX, centerY;
  std::vector<ClassifiedCentroid> centroids;
  while (infile >> centerX >> centerY)
  {
    centroids.push_back({ centerX, centerY, index });
    index++;
  }

  if (strategy == "vh") {
    std::sort(centroids.begin(), centroids.end(), compareByVH());
  } else if (strategy == "sum") {
    std::sort(centroids.begin(), centroids.end(), compareBySum());
  } else {
    std::sort(centroids.begin(), centroids.end(), compareByVV());
  }

  // now lets create the list...
  std::vector<unsigned int> output;

  // also save output to file - it will be helpful for the plots.
  std::ofstream ofs;
  ofs.open(clustersFilePath + "_" + std::to_string(classesNum) + "_floodclasses.txt", std::ofstream::out);

  for (int i = 0; i < classesNum; i++) {
    output.push_back(centroids[i].cl);
    ofs << centroids[i].cl << "\n";
  }

  ofs.close();
  return output;
}

void calculateFloodedAreasFromKMeansOutput(
    std::vector<unsigned int>& floodedAreas,//vector to fill
    unsigned int numberOfClasses,
    unsigned int floodClassesNum,
    int rowsPerDate,
    std::string strategy)
{
  const std::string kmeansResultBasePath = "./.floodsar-cache/kmeans_outputs/" + kmeansInputFilename + "_cl_" + std::to_string(numberOfClasses) + "/";
  const std::string clustersPath = kmeansResultBasePath + std::to_string(numberOfClasses) + "-clusters.txt";
  const std::string pointsPath = kmeansResultBasePath + std::to_string(numberOfClasses) + "-points.txt";
  auto floodClasses = createFloodClassesList(clustersPath, floodClassesNum, strategy);

  // uh oh... programming... i love this shit...
  int iterations = 0;
  unsigned int sum = 0;
  std::ifstream infile(pointsPath);
  int point;
  while (infile >> point)
  {
    iterations++;
    for (auto c : floodClasses) {
      if (point == c) sum++;
    }

    if (iterations == rowsPerDate) {
      // reset
      floodedAreas.push_back(sum);
      sum = 0;
      iterations = 0;
    }
  }
}

int main(int argc, char** argv)
{
  std::cout << "Floodsar start" << '\n';
  createCacheDirectoryIfNotExists();
  GDALAllRegister();

  cxxopts::Options options("Floodsar", " - command line options");

  options.add_options()
    ("a,algorithm", "Choose algorithm. Possible values: 1D, 2D.", cxxopts::value<std::string>())
    ("c,cache-only", "Do not process whole rasters, just use cropped images from cache.")
    ("d,directory", "Path to directory which to search for SAR images", cxxopts::value<std::string>())
    ("h,hydro", "Path to file with hydrological data. Program expects csv.", cxxopts::value<std::string>())
    ("e,extension", "Files with this extension will be attempted to be loaded", cxxopts::value<std::string>())
    ("o,aoi", "Area of Interest file path. The program expects geocoded tiff (GTiff). Content doesn't matter, the program just extracts the bounding box.", cxxopts::value<std::string>())
    ("s,skip-clustering", "Do not perform clustering, assume output files are there")
    ("t,type", "Data type, supported are poc, hype. Use hype if you process Sentinel-1 data. ", cxxopts::value<std::string>()->default_value("hype"))
    ("y,strategy", "Strategy how to pick flood classes. Only applicable to 2D algorithm", cxxopts::value<std::string>()->default_value("vv"))
    ;

  auto userInput = options.parse(argc, argv);
  auto algo = userInput["algorithm"].as<std::string>();
  auto strategy = userInput["strategy"].as<std::string>();
  auto hydroDataCsvFile = userInput["hydro"].as<std::string>();

  // we defaults to 2D version.
  bool isSinglePolVersion = false;
  if (algo == "1D") {
    isSinglePolVersion = true;
  }

  bool cacheOnly = false;
  if (userInput.count("cache-only")) {
    std::cout << "Using images from floodsar-cache" << '\n';
    cacheOnly = true;
    // Choosing this path, we ASSUME .floodsar-cache/cropped folder is healthy and contains images...
  } else {
    auto dirname = userInput["directory"].as<std::string>();
    auto datasetType = userInput["type"].as<std::string>();
    auto rasterExtension = userInput["extension"].as<std::string>();

    RasterInfoExtractor* extractor;

    if (datasetType == "poc") {
      MinimalExampleExtractor e;
      extractor = &e;
      std::cout << "Using PoC minimal extractor...\n";
    } else if (datasetType == "hype") {
      HypeExtractor e;
      extractor = &e;
      std::cout << "Using Hyp3 extractor...\n";
    } else {
      std::cout << "unsupported extractor...\n";
    }

    std::cout << "floodsar: dirname is: " << dirname << '\n';

    std::vector<RasterInfo> rasterPathsBeforeMosaicking = readRasterDirectory(dirname, rasterExtension, extractor);;
    std::vector<RasterInfo> rasterPathsAfterMosaicking;

    std::cout << "floodsar: found " << rasterPathsBeforeMosaicking.size() << "rasters. Now finding dups & mosaicking \n";

    // reprojection 35N->34N
    reprojectIfNeeded(rasterPathsBeforeMosaicking);
    // mosaicking...
    performMosaicking(rasterPathsBeforeMosaicking, rasterPathsAfterMosaicking);

    std::cout << "floodsar: after mosaicking: " << rasterPathsAfterMosaicking.size() << "rasters. Cropping... \n";

    auto areaFilePath = userInput["aoi"].as<std::string>();
    auto areaOfInterestDataset = static_cast<GDALDataset*>(GDALOpen(areaFilePath.c_str(), GA_ReadOnly));
    cropRastersToAreaOfInterest(rasterPathsAfterMosaicking, areaOfInterestDataset);
  }

  HydroDataReader hydroReader;
  std::map<Date, double> obsElevationsMap;
  hydroReader.readFile(obsElevationsMap, hydroDataCsvFile);



  if (isSinglePolVersion) {
    std::cout << "Floodsar algorithm: single-pol (old)\n";

    std::vector<std::string> polarizations { "VH", "VV" };
    for (auto& polarization : polarizations) {
      std::vector<double> elevations; // inaczej water levels
      std::vector<std::string> croppedRasterPaths;

      for (const auto& [day, elevation] : obsElevationsMap) {
        // interesting for us are only dates when we have appropriate picture...
        // so let's use only these...
        // since we check filesystem... it's potential perf. bottleneck...
        const std::string rpath = "./.floodsar-cache/cropped/resampled__" + polarization + "_" + day;
        if (fs::exists(rpath)) {
          elevations.push_back(elevation);
          croppedRasterPaths.push_back(rpath);
        }
      }

      std::vector<double> correlations;
      std::vector<double> thresholds = createSequence(0.01, 0.3, 0.001);

      for (double threshold : thresholds) {
        std::vector<unsigned int> floodedAreaValues;

        for (auto datasetPath : croppedRasterPaths) {
          auto dataset = static_cast<GDALDataset*>(GDALOpen(datasetPath.c_str(), GA_ReadOnly));
          const unsigned int area = calcFloodedArea(dataset, threshold);
          // std::cout << "area for " << datasetPath << "->" << area << '\n';
          floodedAreaValues.push_back(area);
        }

        const double corrCoeff = calcCorrelationCoeff(floodedAreaValues, elevations);

        correlations.push_back(corrCoeff);
      }

      double bestCorrelation = 0.0;
      unsigned int bestThrIndex = 0;

      for (int i = 0; i < correlations.size(); i++) {
        std::cout << "THR: " << thresholds.at(i) << " / COEFF: " << correlations.at(i) << '\n';

        if (bestCorrelation < correlations.at(i)) {
          bestCorrelation = correlations.at(i);
          bestThrIndex = i;
        }
      }

      std::cout << "---------------- RESULTS FOR ---------------- "
        << polarization << " POLARIZATION ---------------" << '\n';
      std::cout << "best threshold: " << thresholds.at(bestThrIndex)
        << " with correlation coefficient of " << correlations.at(bestThrIndex) << '\n';

      std::cout << "also prepare file for mapping procedure...\n";

      std::ofstream outputFileForMapper;
      outputFileForMapper.open( ".floodsar-cache/1d_output/" + polarization, std::ofstream::out);

      for (auto datasetPath : croppedRasterPaths) {
        auto dataset = static_cast<GDALDataset*>(GDALOpen(datasetPath.c_str(), GA_ReadOnly));
        writeThresholdingResultsToFile(dataset, thresholds.at(bestThrIndex), outputFileForMapper);
      }

      outputFileForMapper.close();
    }

  } else {
    std::cout << "Floodsar algorithm: dual-pol (new/improved)\n";

    std::vector<int> numClassesToTry;
    for (int j = 2; j <= 10; j++) {
      numClassesToTry.push_back(j);
    }


    std::ofstream ofs;
    ofs.open("./.floodsar-cache/kmeans_inputs/" + kmeansInputFilename, std::ofstream::out);
    int rowsPerDate = 0; //for starters

    std::cout << "Will create input file for K-Means\n";

    std::vector<double> elevations; // inaczej water levels
    std::vector<std::string> croppedRasterPaths;

    for (const auto& [day, elevation] : obsElevationsMap) {
      // interesting for us are only dates when we have appropriate picture...
      // so let's use only these...
      // since we check filesystem... it's potential perf. bottleneck...
      const std::string vhPath = "./.floodsar-cache/cropped/resampled__VH_" + day;
      const std::string vvPath = "./.floodsar-cache/cropped/resampled__VV_" + day;
      if (fs::exists(vhPath) && fs::exists(vvPath)) {
        elevations.push_back(elevation);
        std::cout << "Elevation for " << day << " = " << elevation << '\n';
        // croppedRasterPathsFor2DCase.push_back({ vhPath, vvPath });

        auto vhDataset = static_cast<GDALDataset*>(GDALOpen(vhPath.c_str(), GA_ReadOnly));
        auto vvDataset = static_cast<GDALDataset*>(GDALOpen(vvPath.c_str(), GA_ReadOnly));

        std::vector<double> vhPixelValues;
        std::vector<double> vvPixelValues;

        getPixelValuesFromRaster(vhDataset, vhPixelValues);
        getPixelValuesFromRaster(vvDataset, vvPixelValues);

        if (rowsPerDate != vhPixelValues.size()) {
          std::cout << "WARNING: Suspicious pixelValues size: " << rowsPerDate << "/" << vhPixelValues.size() << '\n';
          rowsPerDate = vhPixelValues.size();
        }

        for(int i = 0; i < vhPixelValues.size(); i++) {
          ofs << vhPixelValues.at(i) << " " << vvPixelValues.at(i) << "\n";
        }
      }
    }

    ofs.close();

    std::cout << "Input ready. Have " << elevations.size() << " pairs of images matched with hydro data\n";

    if (!userInput.count("skip-clustering")) {
      for (int i : numClassesToTry) {
        performClusteringViaKMeansBinary(kmeansInputFilename, i);
      }
    }

    int indexForLogs = 0;

    double bestCoeff = 0;
    unsigned int bestMaxClasses;
    unsigned int bestFloodClasses;

    for (auto cl : numClassesToTry) {

      unsigned int floodClassesNum = cl / 2;
      while (floodClassesNum) {
        std::vector<unsigned int> floodedAreaValues;
        calculateFloodedAreasFromKMeansOutput(floodedAreaValues, cl, floodClassesNum, rowsPerDate, strategy);
        const double corrCoeff = calcCorrelationCoeff(floodedAreaValues, elevations);

        std::cout << "Calculated correlation [" << indexForLogs << "] allClasses: " << cl
          << " floodClasses: " << floodClassesNum << " - correlationCoeff is " << corrCoeff << "\n";

        if (corrCoeff > bestCoeff) {
          bestCoeff = corrCoeff;
          bestMaxClasses = cl;
          bestFloodClasses = floodClassesNum;
        }

        floodClassesNum--;
        indexForLogs++;
      }

    }

    std::cout << "RESULTS: Best config is: coeff/all classes/flood classes " << bestCoeff << " / " << bestMaxClasses << " / " << bestFloodClasses << "\n";
  }

  return 0;
}

