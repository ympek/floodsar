#include "gdal/cpl_conv.h" // for CPLMalloc()
#include "gdal/gdal_priv.h"
#include "gdal/ogrsf_frmts.h"
#include <algorithm>
#include <cmath>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string_view>
#include <thread>

#include "HydroDataReader.hpp"
#include "RasterInfo.hpp"
#include "csv.hpp"
#include "polarization.hpp"
#include "rasters.hpp"
#include "types.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

const std::string kmeansInputFilename = "KMEANS_INPUT";

void
performClusteringInPlace(std::vector<double>& vectorVH,
                         std::vector<double>& vectorVV,
                         int numClasses, int maxiter)
{

  std::string outDir = ".floodsar-cache/kmeans_outputs/" + kmeansInputFilename +
                       "_cl_" + std::to_string(numClasses);
  fs::create_directory(outDir);

  //  initialization
  int numPoints = vectorVH.size();
  bool updated = false;
  double best;

  // randomization
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<std::mt19937::result_type> mydist(
    0, numPoints); // distribution in range [1, 6]

  std::vector<double> centroidsVH;
  std::vector<double> centroidsVV;

  std::vector<int> clusterAssignments;
  clusterAssignments.resize(numPoints, 0);

  // first initialize
  for (int i = 0; i < numClasses; i++) {
    auto randPoint = mydist(rng);
    centroidsVH.push_back(vectorVH.at(randPoint));
    centroidsVV.push_back(vectorVV.at(randPoint));
  }

  for (int iter = 0; iter < maxiter; iter++) {
    std::cout << "Iter " << iter << "/" << maxiter << "\n";
    updated = false;
    for (int i = 0; i < numPoints; i++) {
      /* find nearest centre for each point */
      best = std::numeric_limits<double>::max(); // positive infinity...
      int newClusterNumber = 0;
      for (int j = 0; j < numClasses; j++) {
        double tmpVH = vectorVH.at(i) - centroidsVH.at(j);
        tmpVH *= tmpVH;

        double tmpVV = vectorVV.at(i) - centroidsVV.at(j);
        tmpVV *= tmpVV;

        double sum = tmpVH + tmpVV;

        if (sum < best) {
          best = sum;
          newClusterNumber = j + 1;
        }
      }
      if (clusterAssignments[i] != newClusterNumber) {
        updated = true;
        clusterAssignments[i] = newClusterNumber;
      }
    }

    // After checking everywhere we look if there was an update
    if (!updated)
      break;

    // recalculate centroids.
    for (int i = 0; i < numClasses; i++) {
      centroidsVH[i] = 0.0;
      centroidsVV[i] = 0.0;
    }

    std::vector<int> counts;
    counts.resize(numClasses, 0);

    for (int i = 0; i < numPoints; i++) {
      auto centroidIndex = clusterAssignments[i] - 1;
      centroidsVH[centroidIndex] += vectorVH[i];
      centroidsVV[centroidIndex] += vectorVV[i];
      counts[centroidIndex]++;
    }

    for (int i = 0; i < numClasses; i++) {
      centroidsVH[i] /= counts[i];
      centroidsVV[i] /= counts[i];
    }
  }

  // dump result.
  std::cout << "Finished clustering. Dump result.";
  const std::string clustersPath =
    outDir + "/" + std::to_string(numClasses) + "-clusters.txt";
  const std::string pointsPath =
    outDir + "/" + std::to_string(numClasses) + "-points.txt";

  std::ofstream ofsClusters;
  ofsClusters.open(clustersPath, std::ofstream::out);

  for (int i = 0; i < numClasses; i++) {
    ofsClusters << centroidsVH[i] << " " << centroidsVV[i] << "\n";
  }

  std::ofstream ofsPoints;
  ofsPoints.open(pointsPath, std::ofstream::out);

  for (int i = 0; i < numPoints; i++) {
    ofsPoints << clusterAssignments[i] << "\n";
  }
}

void
performClusteringViaKMeansBinary(std::string inputFilename, int numClasses)
{
  std::string outDir = ".floodsar-cache/kmeans_outputs/" + inputFilename +
                       "_cl_" + std::to_string(numClasses);

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

void
writeThresholdingResultsToFile(GDALDataset* raster,
                               double threshold,
                               std::ofstream& ofs)
{
  auto rasterBand = raster->GetRasterBand(1);
  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(
    GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

  if (error == CE_Failure) {
    std::cout << "Could not read raster";
  }

  for (int i = 0; i < words; i++) {
    if (buffer[i] < threshold) {
      ofs << "1"
          << "\n";
    } else {
      ofs << "0"
          << "\n";
    }
  }

  CPLFree(buffer);
}

unsigned int
calcFloodedArea(GDALDataset* raster, double threshold)
{
  auto rasterBand = raster->GetRasterBand(1);
  unsigned int floodedArea = 0; // sum of flooded pixels according to threshold;
  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(
    GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

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

void
getProjectionInfo(std::string rasterPath, std::string proj4filePath)
{
  std::string command = "gdalsrsinfo";

  command.append(" --single-line -o epsg ");
  command.append(rasterPath);
  command.append(" >" + proj4filePath);

  std::system(command.c_str());
}

// returns absolute paths to all images that will take part in calculating the
// result
std::vector<RasterInfo>
readRasterDirectory(std::string dirname,
                    std::string fileExtension,
                    RasterInfoExtractor* extractor)
{
  std::vector<RasterInfo> infos;

  std::cout << "Scanning directory: " << dirname << " for " << fileExtension
            << " images..." << std::endl;
  for (auto& p : fs::recursive_directory_iterator(dirname)) {
    auto filepath = p.path();

    auto ext = filepath.extension().string();
    if (ext != fileExtension) {
      // skip if extension does not match
      continue;
    }

    auto name = filepath.filename().string();
    if (name.find("VV") == std::string::npos &&
        name.find("VH") == std::string::npos) {
      // skip if filename does not contain "VV" or "VH" substring
      continue;
    }

    std::string proj4Path = ".floodsar-cache/proj4/" + name + "_proj4";
    // std::cout << "proj4Path " << proj4Path << "\n";
    // std::cout << "Getting projection info for " + filepath.string();
    getProjectionInfo(filepath.string(), proj4Path);

    std::ifstream proj4file(proj4Path);

    RasterInfo extractedInfo = extractor->extractFromPath(filepath.string());

    std::getline(proj4file, extractedInfo.proj4);
    // std::cout << polToString(extractedInfo.pol)+"\n";
    // std::cout << extractedInfo.date+"\n";
    // std::cout << extractedInfo.proj4+"\n";
    // std::cout << "\n";
    infos.push_back(extractedInfo);
  }

  return infos;
}

void
mosaicRasters(const std::string targetPath,
              const std::vector<std::string>& rasterList)
{
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

void
performMosaicking(std::vector<RasterInfo>& rasterInfos,
                  std::vector<RasterInfo>& outputVector)
{
  std::map<std::string, std::vector<RasterInfo>> rastersMap;

  std::vector<std::thread> threads;

  for (int i = 0; i < rasterInfos.size(); i++) {
    auto date = rasterInfos[i].date;
    auto pol = rasterInfos[i].pol;

    auto key = polToString(pol) + "_" + date;

    rastersMap[key].push_back(rasterInfos[i]);
  }

  for (const auto& [key, rasters] : rastersMap) {
    if (rasters.size() == 1) {
      // there is only one raster from this day, no worries.
      std::cout << "Mosaicking not needed.\n";
      outputVector.push_back(rasters.at(0));
    } else if (rasters.size() > 0) {
      std::cout << "Mosaicking REQUIRED.\n";
      std::vector<std::string> pathsOnly;
      for (auto& info : rasters) {
        pathsOnly.push_back(info.absolutePath);
      }

      std::string targetPath = "./.floodsar-cache/vrt/" + key + ".vrt";

      // there is more, probably two. Need to mosaic them.
      threads.push_back(std::thread(mosaicRasters, targetPath, pathsOnly));
      outputVector.push_back(
        { targetPath, rasters.at(0).pol, rasters.at(0).date });
    }
  }

  for (auto& th : threads) {
    th.join();
  }
}

void
createCacheDirectoryIfNotExists()
{
  fs::create_directory(".floodsar-cache");
  fs::create_directory(".floodsar-cache/cropped");
  fs::create_directory(".floodsar-cache/vrt");
  fs::create_directory(".floodsar-cache/reprojected");
  fs::create_directory(".floodsar-cache/proj4");
  fs::create_directory(".floodsar-cache/kmeans_inputs");
  fs::create_directory(".floodsar-cache/kmeans_outputs");
  fs::create_directory(".floodsar-cache/1d_output");
}

std::string
performReprojection(RasterInfo& info, std::string epsgCode)
{
  std::string command = "gdalwarp";
  std::string filename = ".floodsar-cache/reprojected/repd_" +
                         polToString(info.pol) + "_" + info.date;
  // reading cache data prevents some chages
  /*
  if (fs::exists(filename)) {
    std::cout << "skipping reprojection for " << info.date << "_" <<
  polToString(info.pol) << ". File is there..."; return filename;
  }
  */
  command.append(" -t_srs " + epsgCode + " -overwrite ");
  command.append(info.absolutePath);
  command.append(" ");
  command.append(filename);

  std::system(command.c_str());

  return filename;
}

void
reprojectIfNeeded(std::vector<RasterInfo>& rasters, std::string epsgCode)
{
  int reprojectedCount = 0;
  int skippedCount = 0;
  for (auto& it : rasters) {
    std::cout << it.absolutePath + "\n";
    std::cout << "checking if reprojection needed to " << epsgCode + " ...\n";
    if (epsgCode != it.proj4) {
      // needs reprojection.
      std::cout << "... yes\n";
      auto newPath = performReprojection(it, epsgCode);
      std::cout << "reprojection for " << it.absolutePath << " now is "
                << newPath + "\n";
      reprojectedCount++;
      it.absolutePath = newPath;
      // we've changed it, so let's update it. Important for the next step.
      it.proj4 = epsgCode;
    } else {
      std::cout << "... no.\n";
      skippedCount++;
    }
  }

  std::cout << "Reprojection done. Reprojected " << reprojectedCount
            << " images, skipped " << skippedCount << " images\n";
}

void
getPixelValuesFromRaster(GDALDataset* raster,
                         std::vector<double>& pixelValuesVector)
{
  auto rasterBand = raster->GetRasterBand(1);

  const unsigned int xSize = rasterBand->GetXSize();
  const unsigned int ySize = rasterBand->GetYSize();
  const unsigned int words = xSize * ySize;
  double* buffer = static_cast<double*>(CPLMalloc(sizeof(double) * words));
  auto error = rasterBand->RasterIO(
    GF_Read, 0, 0, xSize, ySize, buffer, xSize, ySize, GDT_Float64, 0, 0);

  if (error == CE_Failure) {
    std::cout << "[getPixelValuesFromRaster] Could not read raster\n";
  } else {
    for (int i = 0; i < words; i++) {
      pixelValuesVector.push_back(buffer[i]);
    }
  }

  CPLFree(buffer);
}

struct ClassifiedCentroid
{
  double vh;
  double vv;
  unsigned int cl;
};

struct compareByVH
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vh < c2.vh);
  }
};

struct compareByVV
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vv < c2.vv);
  }
};

struct compareBySum
{
  inline bool operator()(const ClassifiedCentroid& c1,
                         const ClassifiedCentroid& c2)
  {
    return (c1.vv + c1.vh < c2.vv + c2.vh);
  }
};

std::vector<unsigned int>
createFloodClassesList(std::string clustersFilePath,
                       unsigned int classesNum,
                       std::string strategy)
{
  unsigned int index = 1;
  std::ifstream infile(clustersFilePath);
  double centerX, centerY;
  std::vector<ClassifiedCentroid> centroids;
  while (infile >> centerX >> centerY) {
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
  ofs.open(clustersFilePath + "_" + std::to_string(classesNum) +
             "_floodclasses.txt",
           std::ofstream::out);

  for (int i = 0; i < classesNum; i++) {
    output.push_back(centroids[i].cl);
    ofs << centroids[i].cl << "\n";
  }

  ofs.close();
  return output;
}

void
calculateFloodedAreasFromKMeansOutput(
  std::vector<unsigned int>& floodedAreas, // vector to fill
  unsigned int numberOfClasses,
  unsigned int floodClassesNum,
  int rowsPerDate,
  std::string strategy)
{
  const std::string kmeansResultBasePath =
    "./.floodsar-cache/kmeans_outputs/" + kmeansInputFilename + "_cl_" +
    std::to_string(numberOfClasses) + "/";
  const std::string clustersPath =
    kmeansResultBasePath + std::to_string(numberOfClasses) + "-clusters.txt";
  const std::string pointsPath =
    kmeansResultBasePath + std::to_string(numberOfClasses) + "-points.txt";
  auto floodClasses =
    createFloodClassesList(clustersPath, floodClassesNum, strategy);

  // loop for kmeans
  int iterations = 0;
  unsigned int sum = 0;
  std::ifstream infile(pointsPath);
  int point;
  while (infile >> point) {
    iterations++;
    for (auto c : floodClasses) {
      if (point == c)
        sum++;
    }

    if (iterations == rowsPerDate) {
      // reset
      floodedAreas.push_back(sum);
      sum = 0;
      iterations = 0;
    }
  }
}

int
main(int argc, char** argv)
{
  std::cout << "Floodsar start" << '\n';
  createCacheDirectoryIfNotExists();
  GDALAllRegister();

  cxxopts::Options options("Floodsar", " - command line options");

  options.add_options()("a,algorithm",
                        "Choose algorithm. Possible values: 1D, 2D.",
                        cxxopts::value<std::string>()->default_value("1D"))(
    "c,cache-only",
    "Do not process whole rasters, just use cropped images from cache.")(
    "d,directory",
    "Path to directory which to search for SAR images.",
    cxxopts::value<std::string>()->default_value(fs::current_path()))(
    "h,hydro",
    "Path to file with hydrological data. Program expects csv.",
    cxxopts::value<std::string>())(
    "e,extension",
    "Files with this extension will be attempted to be loaded.",
    cxxopts::value<std::string>()->default_value(".tif"))(
    "n,threshold",
    "Comma separated sequence of search space, start,end[,step], e.g.: "
    "0.001,0.1,0.01 for thresholding, or 2,10 for clustering.",
    cxxopts::value<std::vector<std::string>>())(
    "o,aoi",
    "Area of Interest file path. The program expects geocoded tiff (GTiff). "
    "Content doesn't matter, the program just extracts the bounding box.",
    cxxopts::value<std::string>())(
    "p,epsg",
    "Target EPSG code for processing. Should be the same as for the area of "
    "interest. e.g.: EPSG:32630 for UTM 30N.",
    cxxopts::value<std::string>()->default_value("none"))(
    "s,skip-clustering",
    "Do not perform clustering, assume output files are there.")(
    "y,strategy",
    "Strategy how to pick flood classes. Only applicable to 2D algorithm.",
    cxxopts::value<std::string>()->default_value("vv"))(
	"m,maxValue",
    "Clip VV and VH data to this maximum value, e.g. 0.1,0.5 for VV<0.1 and VH<0.5. If not set than wont clip. Only applicable to 2D algorithm.",
    cxxopts::value<std::vector<std::string>>()->default_value("none"))(
    "k,maxiter",
    "Maximum number of kmeans iteration. Only applicable to 2D algorithm.",
    cxxopts::value<std::string>()->default_value("10"));

  auto userInput = options.parse(argc, argv);

  // required parameters
  if (!userInput.count("hydro")) {
    std::cout << "Path to water level data not provided, use -h option. "
                 "Program will quit\n";
    return 0;
  }
  auto hydroDataCsvFile = userInput["hydro"].as<std::string>();

  if (!userInput.count("threshold")) {
    std::cout
      << "Search space not provided, use -n option. Program will quit\n";
    return 0;
  }
  auto thresholdSequence =
    userInput["threshold"].as<std::vector<std::string>>();

  if (!userInput.count("epsg")) {
    std::cout << "EPSG not provided, use -p option. Program will quit\n";
    return 0;
  }
  auto epsgCode = userInput["epsg"].as<std::string>();

  auto algo = userInput["algorithm"].as<std::string>();
  auto strategy = userInput["strategy"].as<std::string>();
  auto maxiter = std::stoi(userInput["maxiter"].as<std::string>());

  // we defaults to 1D version.
  bool isSinglePolVersion = true;
  if (algo == "2D") {
    isSinglePolVersion = false;
  }
  
  auto maxValue = userInput["maxValue"].as<std::vector<std::string>>();

  bool cacheOnly = false;
  if (userInput.count("cache-only")) {
    std::cout << "Using images from floodsar-cache" << '\n';
    cacheOnly = true;
    // Choosing this path, we ASSUME .floodsar-cache/cropped folder is healthy
    // and contains images...
  } else {
    auto dirname = userInput["directory"].as<std::string>();
    auto rasterExtension = userInput["extension"].as<std::string>();

    if (!userInput.count("aoi")) {
      std::cout
        << "Area of interest not provided, use -o option. Program will quit\n";
      return 0;
    }
    auto areaFilePath = userInput["aoi"].as<std::string>();


    RasterInfoExtractor* extractor;
    AsfExtractor e;
    extractor = &e;

    std::vector<RasterInfo> rasterPathsBeforeMosaicking =
      readRasterDirectory(dirname, rasterExtension, extractor);
    std::vector<RasterInfo> rasterPathsAfterMosaicking;

    std::cout << "floodsar: found " << rasterPathsBeforeMosaicking.size()
              << " rasters. Now finding dups & mosaicking \n";

    reprojectIfNeeded(rasterPathsBeforeMosaicking, epsgCode);
    performMosaicking(rasterPathsBeforeMosaicking, rasterPathsAfterMosaicking);

    std::cout << "floodsar: after mosaicking: "
              << rasterPathsAfterMosaicking.size()
              << " rasters. Cropping... \n";

    auto areaOfInterestDataset =
      static_cast<GDALDataset*>(GDALOpen(areaFilePath.c_str(), GA_ReadOnly));
    std::cout << areaFilePath + "\n";
    std::cout << rasterPathsAfterMosaicking[0].absolutePath + "\n";

    cropRastersToAreaOfInterest(
      rasterPathsAfterMosaicking, areaOfInterestDataset, epsgCode);
  }

  HydroDataReader hydroReader;
  std::map<Date, double> obsElevationsMap;
  std::cout << hydroDataCsvFile + "\n";
  hydroReader.readFile(obsElevationsMap, hydroDataCsvFile);
  printMap(obsElevationsMap);
  std::cout << "map ok\n";

  std::ofstream datesFile;

  if (isSinglePolVersion) {
    std::cout << "Floodsar algorithm: single-pol (old)\n";
    std::vector<double> thresholdSequenceDbl(thresholdSequence.size());
    std::transform(thresholdSequence.begin(),
                   thresholdSequence.end(),
                   thresholdSequenceDbl.begin(),
                   [](const std::string& val) { return std::stod(val); });
    std::cout << "threshold from, to, step:\n";
    std::cout << std::to_string(thresholdSequenceDbl[0]) + ", ";
    std::cout << std::to_string(thresholdSequenceDbl[1]) + ", ";
    std::cout << std::to_string(thresholdSequenceDbl[2]);
    std::cout << "\n";

    std::vector<std::string> polarizations{ "VH", "VV" };
    datesFile.open(".floodsar-cache/dates.txt");
    for (auto& polarization : polarizations) {
      std::vector<double> elevations; // i.e. water levels or discharges
      std::vector<std::string> croppedRasterPaths;

      for (const auto& [day, elevation] : obsElevationsMap) {
        // interesting for us are only dates when we have appropriate picture...
        // so let's use only these...
        // since we check filesystem... it's potential perf. bottleneck...
        const std::string rpath =
          "./.floodsar-cache/cropped/resampled__" + polarization + "_" + day;
        if (fs::exists(rpath)) {
          elevations.push_back(elevation);
          croppedRasterPaths.push_back(rpath);
          datesFile << day + "\n";
        }
      }
      datesFile.close();
      std::vector<double> correlations;
      std::vector<double> thresholds = createSequence(thresholdSequenceDbl[0],
                                                      thresholdSequenceDbl[1],
                                                      thresholdSequenceDbl[2]);

      for (double threshold : thresholds) {
        std::vector<unsigned int> floodedAreaValues;

        for (auto datasetPath : croppedRasterPaths) {
          auto dataset = static_cast<GDALDataset*>(
            GDALOpen(datasetPath.c_str(), GA_ReadOnly));
          // std::cout << datasetPath + "\n";
          const unsigned int area = calcFloodedArea(dataset, threshold);
          // std::cout << "area for " << datasetPath << "->" << area << '\n';
          floodedAreaValues.push_back(area);
          GDALClose(dataset);
        }

        const double corrCoeff =
          calcCorrelationCoeff(floodedAreaValues, elevations);

        correlations.push_back(corrCoeff);
      }

      double bestCorrelation = 0.0;
      unsigned int bestThrIndex = 0;

      for (int i = 0; i < correlations.size(); i++) {
        // std::cout << "THR: " << thresholds.at(i)
                  // << " / COEFF: " << correlations.at(i) << '\n';

        if (bestCorrelation < correlations.at(i)) {
          bestCorrelation = correlations.at(i);
          bestThrIndex = i;
        }
      }

      std::cout << "---------------- RESULTS FOR ---------------- "
                << polarization << " POLARIZATION ---------------" << '\n';
      std::cout << "best threshold: " << thresholds.at(bestThrIndex)
                << " with correlation coefficient of "
                << correlations.at(bestThrIndex) << '\n';

      std::cout << "also prepare file for mapping procedure...\n";

      std::ofstream outputFileForMapper;
      outputFileForMapper.open(".floodsar-cache/1d_output/" + polarization,
                               std::ofstream::out);

      for (auto datasetPath : croppedRasterPaths) {
        auto dataset =
          static_cast<GDALDataset*>(GDALOpen(datasetPath.c_str(), GA_ReadOnly));
        writeThresholdingResultsToFile(
          dataset, thresholds.at(bestThrIndex), outputFileForMapper);
      }

      outputFileForMapper.close();
    }

  } else {
    std::cout << "Floodsar algorithm: dual-pol (new/improved)\n";
    std::vector<int> thresholdSequenceInt(thresholdSequence.size());
    std::transform(thresholdSequence.begin(),
                   thresholdSequence.end(),
                   thresholdSequenceInt.begin(),
                   [](const std::string& val) { return std::stoi(val); });
    std::cout << "classes from, to:\n";
    std::cout << std::to_string(thresholdSequenceInt[0]) + ", ";
    std::cout << std::to_string(thresholdSequenceInt[1]);
    std::cout << "\n";

    std::vector<int> numClassesToTry;
    for (int j = thresholdSequenceInt[0]; j <= thresholdSequenceInt[1]; j++) {
      numClassesToTry.push_back(j);
    }

    std::ofstream ofs;
    ofs.open("./.floodsar-cache/kmeans_inputs/" + kmeansInputFilename,
             std::ofstream::out);
    int rowsPerDate = 0; // for starters

    std::cout << "Will create input file for K-Means\n";

    std::vector<double> elevations; // these are water levels or discharges
    std::vector<std::string> croppedRasterPaths;
    datesFile.open(".floodsar-cache/dates.txt");

    // new kmeans impl
    std::vector<double> vhAllPixelValues;
    std::vector<double> vvAllPixelValues;
    for (const auto& [day, elevation] : obsElevationsMap) {
      // interesting for us are only dates when we have appropriate picture...
      // so let's use only these...
      // since we check filesystem... it's potential perf. bottleneck...
      const std::string vhPath =
        "./.floodsar-cache/cropped/resampled__VH_" + day;
      const std::string vvPath =
        "./.floodsar-cache/cropped/resampled__VV_" + day;

      if (fs::exists(vhPath) && fs::exists(vvPath)) {
        elevations.push_back(elevation);
        std::cout << "Elevation for " << day << " = " << elevation << '\n';
        // croppedRasterPathsFor2DCase.push_back({ vhPath, vvPath });
        datesFile << day + "\n";

        auto vhDataset =
          static_cast<GDALDataset*>(GDALOpen(vhPath.c_str(), GA_ReadOnly));
        auto vvDataset =
          static_cast<GDALDataset*>(GDALOpen(vvPath.c_str(), GA_ReadOnly));

        std::vector<double> vhPixelValues;
        std::vector<double> vvPixelValues;

        getPixelValuesFromRaster(vhDataset, vhPixelValues);
        getPixelValuesFromRaster(vvDataset, vvPixelValues);

        if (rowsPerDate != vhPixelValues.size()) {
          std::cout << "WARNING: Suspicious pixelValues size: " << rowsPerDate
                    << "/" << vhPixelValues.size() << '\n';
          rowsPerDate = vhPixelValues.size();
        }

        for (int i = 0; i < vhPixelValues.size(); i++) {
          ofs << vhPixelValues.at(i) << " " << vvPixelValues.at(i) << "\n";
          vhAllPixelValues.push_back(vhPixelValues.at(i));
          vvAllPixelValues.push_back(vvPixelValues.at(i));
        }
      }
    }

    datesFile.close();
    ofs.close();
	
	if(maxValue[0] != "none") {
		std::vector<double> maxValueDbl(maxValue.size());
		std::transform(maxValue.begin(),
					   maxValue.end(),
					   maxValueDbl.begin(),
					   [](const std::string& val) { return std::stod(val); });
		std::cout << "clipping max values to VV, VH:\n";
		std::cout << std::to_string(maxValueDbl[0]) + ", ";
		std::cout << std::to_string(maxValueDbl[1]) + ", ";
		std::cout << "\n";
		
		for (int i=0; i<vvAllPixelValues.size(); i++) if(vvAllPixelValues[i] > maxValueDbl[0]) vvAllPixelValues[i]=maxValueDbl[0];
		for (int i=0; i<vhAllPixelValues.size(); i++) if(vhAllPixelValues[i] > maxValueDbl[1]) vhAllPixelValues[i]=maxValueDbl[1];
	} else {
		std::cout << "No clipping VV and VH values.\n";
	}
	
    std::cout << "Input ready. Have " << elevations.size()
              << " pairs of images matched with hydro data\n";

    if (!userInput.count("skip-clustering")) {
      for (int i : numClassesToTry) {
        // performClusteringViaKMeansBinary(kmeansInputFilename, i);
        performClusteringInPlace(vhAllPixelValues, vvAllPixelValues, i, maxiter);
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
        calculateFloodedAreasFromKMeansOutput(
          floodedAreaValues, cl, floodClassesNum, rowsPerDate, strategy);
        const double corrCoeff =
          calcCorrelationCoeff(floodedAreaValues, elevations);

        std::cout << "Calculated correlation [" << indexForLogs
                  << "] allClasses: " << cl
                  << " floodClasses: " << floodClassesNum
                  << " - correlationCoeff is " << corrCoeff << "\n";

        if (corrCoeff > bestCoeff) {
          bestCoeff = corrCoeff;
          bestMaxClasses = cl;
          bestFloodClasses = floodClassesNum;
        }

        floodClassesNum--;
        indexForLogs++;
      }
    }

    const std::string bestClassPath =".floodsar-cache/kmeans_outputs/best.txt";
    std::ofstream ofsBestClass;
    ofsBestClass.open(bestClassPath, std::ofstream::out);
    ofsBestClass <<  bestMaxClasses << " " << bestFloodClasses << "\n";
    ofsBestClass.close();

    std::cout << "RESULTS: Best config is: coeff/all classes/flood classes "
              << bestCoeff << " / " << bestMaxClasses << " / "
              << bestFloodClasses << "\n";
  }

  LOG("test logging %s %d", "carrot", 5);
  return 0;
}
