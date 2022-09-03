#pragma once

#include <thread>
#include "XYPair.hpp"
#include "BoundingBox.hpp"
#include "RasterInfo.hpp"
#include "gdal/gdal_priv.h"

BoundingBox getRasterBoundingBox(GDALDataset* raster) {
  BoundingBox boundingBox;

  const int widthInPixels  = raster->GetRasterBand(1)->GetXSize();
  const int heightInPixels = raster->GetRasterBand(1)->GetYSize();

  boundingBox.widthInPixels = widthInPixels;
  boundingBox.heightInPixels = heightInPixels;

  double affineTransform[6];

  raster->GetGeoTransform(affineTransform);

  auto calcPixelCoords = [&affineTransform](double pixelX, double pixelY) {
    double geoX = affineTransform[0] + pixelX * affineTransform[1] + pixelY * affineTransform[2];
    double geoY = affineTransform[3] + pixelX * affineTransform[4] + pixelY * affineTransform[5];
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

void cropToZone(BoundingBox zoneBBox, RasterInfo info)
{
  std::string command = "gdal_translate";
  command.append(" -strict -r bilinear -outsize ");
  command.append(std::to_string(zoneBBox.widthInPixels));
  command.append(" ");
  command.append(std::to_string(zoneBBox.heightInPixels));
  command.append(" -projwin_srs " + info.proj4+ " -projwin ");
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

  command.append(".floodsar-cache/cropped/resampled__" + polToString(info.pol) + "_" + info.date);

  std::system(command.c_str());
}


void cropRastersToAreaOfInterest(std::vector<RasterInfo>& rasterPaths, GDALDataset* aoiDataset)
{
  const unsigned int maxThreads = std::thread::hardware_concurrency();
  auto zoneBB = getRasterBoundingBox(aoiDataset);

  // the overhead of creating threads might be a problem later when there is more rasters to process
  // Ideally, there should be some sort of queue system and number of threads fixed on hardware_concurrency
  // I don't know if I will be able to implement this properly in such short time.
  std::vector<std::thread> threads;

  std::cout << "processing " << rasterPaths.size() << " rasters \n";

  for (int i = 0; i < rasterPaths.size(); i++) {
    std::cout << i << ". process " << rasterPaths.at(i).absolutePath << '\n';
    // non-threaded v for debugging:
    // cropToZone(zoneBB, rasterPaths.at(i));
    threads.push_back(std::thread(cropToZone, zoneBB, rasterPaths.at(i)));
  }

  for (auto &th : threads) {
    th.join();
  }
}

void printProjCrs(RasterInfo info) {
  std::string command = "gdalsrsinfo";
  command.append(" --single-line -o epsg");
  command.append(" ");
  command.append(info.absolutePath);

  std::system(command.c_str());
}
