#pragma once

#include <random>
#include <fstream>

/*
* The 2D algorithm that performs clustering on two SAR polarizations at the same time.

@param vectorVH is a cross polarization
@param vectorVV is a co-polarization
@param numClasses is a number of flooded classes
@param maxiter is a maximum number of iteration to find clusters
@param frac is a fraction of pixels that algorithms analyzes in order to find cluster centroids.

*/
const std::string kmeansInputFilename = "KMEANS_INPUT";
const int kmeansMinimumPoints = 100;
/*
*
* Function performs k-means clustering for floodSar
*
*/
void
performClustering(std::vector<double>& vectorVH,
    std::vector<double>& vectorVV,
    int numClasses, int maxiter, double frac)
{

    std::string outDir = ".floodsar-cache/kmeans_outputs/" + kmeansInputFilename +
        "_cl_" + std::to_string(numClasses);
    fs::create_directory(outDir);

	
	//  initialization
    int numPoints = vectorVH.size();
    int numPointsFrac = round(numPoints * frac);
    bool updated = false;
    double best;


    if (numPointsFrac < 1) numPointsFrac = kmeansMinimumPoints;
    if (numPointsFrac > numPoints) numPointsFrac = numPoints;
    std::cout << "Clustering sample: " << numPointsFrac  << " / All pixels: " << numPoints << "\n";
    std::vector<size_t> allPointsInd(numPoints);
    for (int i = 0; i < numPoints; i++) allPointsInd[i] = i;
    std::vector<size_t> fracInd;
	if(frac < 1)
		std::sample(allPointsInd.begin(), allPointsInd.end(), std::back_inserter(fracInd), numPointsFrac, std::mt19937{ std::random_device{}() });
	else
		fracInd = allPointsInd;
		

    // randomization
    std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_int_distribution<std::mt19937::result_type> mydist(
        0, numPoints); 

    std::vector<double> centroidsVH;
    std::vector<double> centroidsVV;

    std::vector<int> clusterAssignments;
    std::vector<int> clusterAssignmentsFrac;
    clusterAssignments.resize(numPoints, 0);
    clusterAssignmentsFrac.resize(numPointsFrac, 0);

    // first initialize
    for (int i = 0; i < numClasses; i++) {
        auto randPoint = mydist(rng);
        centroidsVH.push_back(vectorVH.at(randPoint));
        centroidsVV.push_back(vectorVV.at(randPoint));
    }

    //Find clusters based on a fraction of the data
    for (int iter = 0; iter < maxiter; iter++) {
        std::cout << "Iter " << iter << "/" << maxiter << "\n";
        updated = false;
        for (int i = 0; i < numPointsFrac; i++) {
            /* find nearest centre for each point */
            int ii = fracInd[i];
            best = std::numeric_limits<double>::max(); // positive infinity...
            int newClusterNumber = 0;
            for (int j = 0; j < numClasses; j++) {

                double tmpVH = vectorVH.at(ii) - centroidsVH.at(j);
                tmpVH *= tmpVH;
                double tmpVV = vectorVV.at(ii) - centroidsVV.at(j);
                tmpVV *= tmpVV;

                double sum = tmpVH + tmpVV;

                if (sum < best) {
                    best = sum;
                    newClusterNumber = j + 1;
                }
            }
            if (clusterAssignmentsFrac[i] != newClusterNumber) {
                updated = true;
                clusterAssignmentsFrac[i] = newClusterNumber;
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

        for (int i = 0; i < numPointsFrac; i++) {
            auto centroidIndex = clusterAssignmentsFrac[i] - 1;
            int ii = fracInd[i];
            centroidsVH[centroidIndex] += vectorVH[ii];
            centroidsVV[centroidIndex] += vectorVV[ii];
            counts[centroidIndex]++;
        }

        for (int i = 0; i < numClasses; i++) {
            centroidsVH[i] /= counts[i];
            centroidsVV[i] /= counts[i];
        }

    }

    // now label all pixels based on the earlier clustering
    std::cout << "Labelling all pixels...\n";
    for (int i = 0; i < numPoints; i++) {

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
        clusterAssignments[i] = newClusterNumber;
    }


    // dump result.
    std::cout << "Finished clustering. Dump result...\n";
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
/*
Function that returns a vector with classes list
*
* @params clusersFilePath is a prefix for filenemame that stores flood mapping results
* @params classesNum is a number of flood classes
* @params is a strategy how to pick flood classes. Only applicable to 2D algorithm. Possible values: vh, vv, sum.
* 
*/
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
/*
Function to calculate flooder Areas
@params floodedAreas is a vector that is filled in during function invocation. 
@params numberofClasses iz  total number of classes analyzed by k-means
@params floodClassesNum is a number of flood classes to calculate area
*/
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