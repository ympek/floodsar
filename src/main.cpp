#include "gdal/cpl_conv.h" // for CPLMalloc()
#include "gdal/gdal_priv.h"
#include "gdal/ogrsf_frmts.h"
#include <algorithm>
#include <cmath>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

#include "HydroDataReader.hpp"
#include "RasterInfo.hpp"
#include "csv.hpp"
#include "polarization.hpp"
#include "rasters.hpp"
#include "types.hpp"
#include "utils.hpp"
#include "clustering.hpp"

/*
* Main function file
*/

namespace fs = std::filesystem;


int
main(int argc, char** argv)
{
  std::cout << "Floodsar start" << '\n';
  GDALAllRegister();

  cxxopts::Options options("Floodsar", " - command line options");

  options.add_options()
    ("h,help", "Print this help")
    ("a,algorithm",
    "Choose algorithm. Possible values: 1D, 2D.",
    cxxopts::value<std::string>()->default_value("1D"))(
    "c,cache-only",
    "Do not process whole rasters, just use cropped images from cache.")(
    "t,stdParser",
    "Use standard, i.e. YYYYMMDD_POL.extm, names parser instead of ASF HyP3 names .")(
    "l,conv-to-dB",
    "Convert linear power to dB (log scale) befor clustering. Only for the 2D algorithm. Recommended.")(
    "d,directory",
    "Path to directory which to search for SAR images.",
    cxxopts::value<std::string>()->default_value(fs::current_path()))(
    "g,gauge",
    "Path to file with river gauge (hydrological) data. Program expects csv.",
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
    cxxopts::value<std::string>()->default_value("100"))(
    "f,fraction",
    "Fraction of pixels used to perform kmeans clustering. Only applicable to 2D algorithm.",
     cxxopts::value<std::string>()->default_value("1.0"));

  auto userInput = options.parse(argc, argv);
  
  // print help
  if (userInput.count("help"))
    {
        std::cout << options.help() << "\n";
        return 0;
    }
    
  // required parameters
  if (!userInput.count("gauge")) {
    std::cout << "Path to water level data not provided, use -g option. \n"<< options.help() << "\nProgram will quit\n";
    return 0;
  }
  auto hydroDataCsvFile = userInput["gauge"].as<std::string>();

  if (!userInput.count("threshold")) {
    std::cout
      << "Search space not provided, use -n option.\n"<< options.help() << "\nProgram will quit\n";
    return 0;
  }
  auto thresholdSequence =
    userInput["threshold"].as<std::vector<std::string>>();

  if (!userInput.count("epsg")) {
    std::cout << "EPSG not provided, use -p option.\n"<< options.help() << "\nProgram will quit\n";
    return 0;
  }
  
  auto epsgCode = userInput["epsg"].as<std::string>();
  auto algo = userInput["algorithm"].as<std::string>();
  auto strategy = userInput["strategy"].as<std::string>();
  auto maxiter = std::stoi(userInput["maxiter"].as<std::string>());
  auto fraction = std::stod(userInput["fraction"].as<std::string>());
  if (fraction < 0 | fraction > 1.0) {
      std::cout <<"Fraction of pixels is not in (0.0, 1.0>: "<< fraction << " Program will quit\n";
      return 0;
  }
  
  //in case of pixel value = 0 durinf lin to dB conversion
  const double minValueDbl = -40.0;

  // we defaults to 1D version.
  bool isSinglePolVersion = true;
  if (algo == "2D") {
    isSinglePolVersion = false;
  }
  
  auto maxValue = userInput["maxValue"].as<std::vector<std::string>>();

  bool convToDB = false;
  if (userInput.count("conv-to-dB")) convToDB = true;

  bool cacheOnly = false;
  if (userInput.count("cache-only")) {
    createCacheDirectoryIfNotExists();
    std::cout << "Using images from floodsar-cache" << '\n';
    cacheOnly = true;
    // Choosing this path, we ASSUME .floodsar-cache/cropped folder is healthy
    // and contains images...
  } else {
    std::cout << "Creating new cache directory" << '\n';
    if(fs::exists(".floodsar-cache")) fs::remove_all(".floodsar-cache");
    createCacheDirectoryIfNotExists();

    auto dirname = userInput["directory"].as<std::string>();
    auto rasterExtension = userInput["extension"].as<std::string>();

    if (!userInput.count("aoi")) {
      std::cout
        << "Area of interest not provided, use -o option. Program will quit\n";
      return 0;
    }
    auto areaFilePath = userInput["aoi"].as<std::string>();

    RasterInfoExtractor* extractor;

    if (userInput.count("stdParser")) {
        StdExtractor e;
        extractor = &e;
        std::cout << "Using standard names parser - expecting YYYYMMDD_POL.ext\n";
    }
    else {
        AsfExtractor e;
        extractor = &e;
        std::cout << "Using ASF HyP3 names parser.\n";
    }
  
    

    std::vector<RasterInfo> rasterPathsBeforeMosaicking =
      readRasterDirectory(dirname, rasterExtension, extractor);
    std::vector<RasterInfo> rasterPathsAfterMosaicking;
    std::for_each(rasterPathsBeforeMosaicking.begin(), rasterPathsBeforeMosaicking.end(), [](RasterInfo& r) {
        if (polToString(r.pol) == "ERROR") {
            std::cout << "parsing error with: " << r.absolutePath << "\n";
            exit(1);
            }
        });
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
		if(j==1) continue;
		numClassesToTry.push_back(j);
		}
	if(numClassesToTry.size() == 0)
	{
		std::cout << "to few classes for k-means, increase the -n parameter range\nProgram will quit\n";
		return 0;
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
    if (convToDB) {
        std::cout << "Converting linear power to dB.\n";
        for (int i = 0; i < vvAllPixelValues.size(); i++) if (vvAllPixelValues[i] > 0) { vvAllPixelValues[i] = 10.0 * log10(vvAllPixelValues[i]); } else { vvAllPixelValues[i] = minValueDbl; }
        for (int i = 0; i < vhAllPixelValues.size(); i++) if (vhAllPixelValues[i] > 0) { vhAllPixelValues[i] = 10.0 * log10(vhAllPixelValues[i]); } else { vhAllPixelValues[i] = minValueDbl; }
    }



    std::cout << "Input ready. Have " << elevations.size()
              << " pairs of images matched with gauge data\n";

    if (!userInput.count("skip-clustering")) {
      for (int i : numClassesToTry) {
		  performClustering(vhAllPixelValues, vvAllPixelValues, i, maxiter, fraction);
		  }
    }

    int indexForLogs = 0;

    double bestCoeff = 0;
    unsigned int bestMaxClasses;
    unsigned int bestFloodClasses;

    for (int cl : numClassesToTry) {

      unsigned int floodClassesNum = cl-1;
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
