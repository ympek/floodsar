#pragma once

#include "BoundingBox.hpp"
#include "RasterInfo.hpp"
#include "XYPair.hpp"
#include "gdal/gdal_priv.h"
#include <thread>
#include <fstream>

BoundingBox
getRasterBoundingBox(GDALDataset* raster)
{
  BoundingBox boundingBox;

  const int widthInPixels = raster->GetRasterBand(1)->GetXSize();
  const int heightInPixels = raster->GetRasterBand(1)->GetYSize();

  boundingBox.widthInPixels = widthInPixels;
  boundingBox.heightInPixels = heightInPixels;

  double affineTransform[6];

  raster->GetGeoTransform(affineTransform);

  auto calcPixelCoords = [&affineTransform](double pixelX, double pixelY) {
    double geoX = affineTransform[0] + pixelX * affineTransform[1] +
                  pixelY * affineTransform[2];
    double geoY = affineTransform[3] + pixelX * affineTransform[4] +
                  pixelY * affineTransform[5];
    return XYPair(geoX, geoY);
  };

  auto upperLeftCorner = calcPixelCoords(0, 0);
  auto lowerRightCorner = calcPixelCoords(widthInPixels, heightInPixels);

  boundingBox.upperLeftX = upperLeftCorner.x;
  boundingBox.upperLeftY = upperLeftCorner.y;
  boundingBox.lowerRightX = lowerRightCorner.x;
  boundingBox.lowerRightY = lowerRightCorner.y;

  return boundingBox;
}

void
cropToZone(BoundingBox zoneBBox, RasterInfo info, std::string epsgCode)
{
  std::string command = "gdal_translate";
  command.append(" -strict -r bilinear -outsize ");
  command.append(std::to_string(zoneBBox.widthInPixels));
  command.append(" ");
  command.append(std::to_string(zoneBBox.heightInPixels));
  command.append(" -projwin_srs " + epsgCode + " -projwin ");
  command.append(std::to_string(zoneBBox.upperLeftX));
  command.append(" ");
  command.append(std::to_string(zoneBBox.upperLeftY));
  command.append(" ");
  command.append(std::to_string(zoneBBox.lowerRightX));
  command.append(" ");
  command.append(std::to_string(zoneBBox.lowerRightY));
  command.append(" ");
  command.append(info.absolutePath);
  command.append(" ");

  command.append(".floodsar-cache/cropped/resampled__" + polToString(info.pol) +
                 "_" + info.date);

  std::cout << command << "\n";
  std::system(command.c_str());
}

void
cropRastersToAreaOfInterest(std::vector<RasterInfo>& rasterPaths,
                            GDALDataset* aoiDataset,
                            std::string epsgCode)
{
  const unsigned int maxThreads = std::thread::hardware_concurrency();
  auto zoneBB = getRasterBoundingBox(aoiDataset);

  // the overhead of creating threads might be a problem later when there is
  // more rasters to process Ideally, there should be some sort of queue system
  // and number of threads fixed on hardware_concurrency
  // now it works in a number of tests
  std::vector<std::thread> threads;

  std::cout << "processing " << rasterPaths.size() << " rasters \n";

  for (int i = 0; i < rasterPaths.size(); i++) {
    std::cout << i << ". process " << rasterPaths.at(i).absolutePath << '\n';
    // non-threaded v for debugging:
    cropToZone(zoneBB, rasterPaths.at(i), epsgCode);
    // threads.push_back(std::thread(cropToZone, zoneBB, rasterPaths.at(i),
    // epsgCode));
  }

  for (auto& th : threads) {
    th.join();
  }
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

void
printProjCrs(RasterInfo info)
{
  std::string command = "gdalsrsinfo";
  command.append(" --single-line -o epsg");
  command.append(" ");
  command.append(info.absolutePath);

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
