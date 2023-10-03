# floodsar

Software package for flood extent mapping using SAR imagery time series and river gauge data.

## Prerequisites:

### Software

Assuming recent Debian-based Linux distribution (Ubuntu, Mint, etc.)

Install dependencies required to build:

```sudo apt install build-essential g++ cmake ninja-build libgdal-dev```

Clone the repository:

```git clone https://github.com/ympek/floodsar```

Enter the directory:

```cd floodsar```

Use build script to build:

```
./build.sh
```

Then run with:

```
build/floodsar <options>
```

### Data 
* SAR time series, e.g. Sentinel-1. The more the better, probably (e.g. 20 dual-pol images or more)
* Water levels or discharge data from a river gauge in the area (CSV file with dates and values is expected)
* Geotiff describing AOI (Area of interest). The program will crop all images to this area. Only the bounding box of the geotiff is needed, pixel values don't matter. If you have already cropped images, this step can be skipped.

## Usage / use cases

### Basic

If you have uncropped/unprepared SAR time series, the program will have to crop the images to area of interest first (`-o` parameter). If more than one image has the same date, the program will perform mosaicking of the files. There is also option of reprojecting the images implemented as a command line parameter (`-e`).

The SAR images time series should be stored in one directory (provided using the `-d` parameter), should have the same extension and a specified pattern of names as in [ASF RTC](https://hyp3-docs.asf.alaska.edu/guides/rtc_product_guide/#naming-convention) naming convention:

`S1x_yy_aaaaaaaaTbbbbbb_ppo_RTCzz_u_defklm_ssss_pol.extension`

where the key elements are:
* `aaaaaaaa`, which is the `YYYYMMDD` date of the image, eg. 20150121 for the 21st of January 2015.
* `pol`, which is a polarization of the image; either `VV` or `VH`.
* `extension`, which is the image file extension, e.g, `.tif`, specified using the `-e` parameter (default is `.tif`).

In fact the program will look for the specified above keywords by splitting the file name at the places of the underscore character (`_`), therefore if your files do not come form the ASF RTC on demand processing just rename them to match the pattern above while keeping the appropriate  date, polarization and extension.

The .csv file (provided using the `-h` parameter) with the river gauge data should have two comma-separated columns without any header (no column names). The first column should have a YYYYMMDD date of the observation and the second column should have a numerical value of the observation (water level or discharge), e.g:

```
20150121,1.2

20150122,1.4

20150123,1.5

20150124,1.2

[...]
```

There can be more observations in the csv file than SAR images. The program will match images with the csv file content by date.

The program has two algorithms for flood mapping (`-a` parameter) 1D and 2D:

* The default 1D algorithm looks for an optimal threshold below which the SAR pixels values are labeled as "flooded". The detailed description of the algorithm is provided in [Berezowski et al. 2019](https://ieeexplore.ieee.org/abstract/document/8899239). In short, the optimal threshold is chosen by the highest correlation of the labeled flooding area and the river gauge observations from the .csv file. Note: this is _not_ the Otsu method. Please set the following parameters for `-a 1D` algorithm:
   - `-n`, the comma-separated search space: `start,end,step`. If the SAR images are in dB a good starting point would be `-n 0.001,0.2,0.001` to search for the threshold value from 0.001 to 0.2 with a step of 0.001.
* The 2D algorithm performs a k-means clustering of a two-dimensional (VV and VH) dataset containing all SAR images. In the second step, an optimal set of clusters that has a highest correlation with the river gauge observation is chosen. This algorithm is _similar_ to multiclass Otsu due to utilization of k-means, but works on multiple images at the same time and points out which cluster combinations are the best for flood labeling. The set may have one or more clusters labeled as flooded depending on the case. The remaining clusters are labeled as non-flooded. Please set the following parameters for `-a 2D` algorithm:
   - `-n`, the comma-separated number of target clusters to try with kmeans: `start,end`. A good starting point would be `-n 2,5` to test kmeans results with 2, 3, 4, and 5 target clusters.
   - `-m`, the comma-separated list of maximum backscattering value for clustering: `VV,VH`. This is an important parameter, which limits the two-dimensional space in which the clustering is performed to a maximum value separately for each polarization. If too high values are present in the dataset the kmeans will have difficulties to find properly flooded clusters. Good maximum values in the case of dB data would be `-m 0.1,05` to set maximum value of 0.1 for VV and 0.5 for VH data. Mind that if data is in linear power or other units a different set of maximum will be required than in this example. The best is to analyze the histogram of few VV and VH images before setting this parameter.
   - `-k`, the maximum number of k-means iterations. A too-low number, e.g. 2, will result in poor clustering and a too-high number will take a long time to process. A good starting point would be the default `-k 100`. After running with 100 iterations one may check if the algorithm converged. For large data sets about 1000 iterations may be needed. Small values are good for testing. 
   - `-y`, cluster centroid comparing strategy. Possible values: vh, vv, sum. Once clustering is done, the program is supposed to label N 'darkest' clusters as flooded. But how to determine if one cluster is 'darker' than the other? This parameter determines it. If value is 'vh', we sort centroids based on the value in VH polarization; 'vv' works analogously for VV polarization. 'sum' value means we compare centroids based on the sum i.e. centroid1 is darker than centroid2 if c1.vh + c1.vv < c2.vh + c2.vv. The best is to check results from all strategies.
   - `-s`, --skip-clustering - this option can be used to speed up checking different strategies (`-y` parameter). Clustering results are cached - so if we want to change the sorting strategy, we don't have to repeat K-means - add this parameter to use K-means outputs cached on disk, so results will be instant.

The program will keep the pre-processed images in cache, therefore any subsequent runs should be executed with `--cache-only` or '-c', as we have to crop images only once for a given dataset. In this case the SAR images directory path `-d` wont be required to run the program.

### Example

To run the program with the 1D algorithm, search space 0.001 to 0.1 with a step of 0.001, a relative path to an AOI file: data/aoi.tif, a relative path to a .csv file: data/levels.csv, a relative path to SAR imagery directory with .tif images: data/sarInput, and the UTM 30N (EPSG:32630) coordinate system use:

`cd build` to go to the directory with the floodsar executable

`./floodsar -d data/sarInput -a data/aoi.tif -h data/levels.csv -n 0.001,0.1,0.001 -p EPSG:32630`

to create output flood maps use 

`./mapper -b VV`

if the VV polarization thresholding gives better results, or

`./mapper -b VH`

if the VH polarization thresholding is preferred.

To re-run the algorithm with a broader search space and the cached data from the previous step (no preprocessing), use:

`./floodsar -c -h data/levels.csv -n 0.001,0.3,0.001`

To run the program with the 2D algorithm, the number of k-means clusters from 3 to 6 and 11 iterations, maximum value of VV=0.2 and maximum value of VH=0.4, a relative path to an AOI file: data/aoi.tif, a relative path to a .csv file: data/levels.csv, a relative path to SAR imagery directory with .tif images: data/sarInput, and the UTM 30N (EPSG:32630) coordinate system use:

`./floodsar -a 2D -d data/sarInput -a data/aoi.tif -h data/levels.csv -n 3,6 -k 11 -m 0.2,0.4 -p EPSG:32630`

to create output flood maps, use `mapper` binary.  You can use the best combination of classes as determined by the floodsar output with the `-a` option:

`./mapper -a`

You will see where individual tiff files were saved. Alternatively, mapper may use two arguments, `-c` (number of all classes) and `-f` (number of classes marked as flooded) to review other class combinations. For example, to create maps for the case that we have 3 classes and one of them is flooded, type:

`./mapper -c 3 -f 1`

Generated maps will be available in `./mapped` folder. They can be loaded into GIS software (e.g. QGIS) to examine them.

### Using manually pre-cropped imagery

If the SAR time series are already cropped to area of interest by the user, we can inject cropped images right into programs' cache. Expected naming for images is:
`resampled__<POLARIZATION>_<DATE>`, where date is YYYYMMDD. Example: resampled__VH_20170228

Images should be put into the sub-folder `build/.floodsar-cache/cropped/` of the `floodsar` directory.

From now on, we can run the program with `--cache-only` or `-c` option instead of providing the SAR images directory `-d`, AOI file `-o` and coordinate system `-p`:

`./floodsar --cache-only --hydro data/levels.csv [other parameters]`

or for 2D algorithm:

`./floodsar --cache-only --hydro data/levels.csv --algorithm 2D [other parameters]`

## Command line options

Here is a comprehensive reference of available options for `floodsar`.

| Option       |      Description      | Default|
|:-------------|:-------------|:-------------:|
| --algorithm<br />-a |  Choose algorithm. Possible values: 1D, 2D. |1D|
| --aoi<br />-o| Area of Interest file path. The program expects geocoded tiff (GTiff). Content doesn't matter, the program just extracts the bounding box.|--|
| --cache-only<br />-c|    Do not process whole rasters, just use cropped images from cache.   |--|
| --directory<br />-d| Path to directory which to search for SAR images. |--|
| --epsg<br />-p | Target EPSG code for processing. Should be the same as for the AOI (-o). e.g.: EPSG:32630 for UTM 30N. |--|
| --extension<br />-e| Files with this extension will be attempted to be loaded (.tif, .img or else. Leading dot is required). |.tif|
| --hydro<br />-h| Path to file with hydrological data. Program expects two column csv: date YYYYMMDD, water elevation/discharge.   |--|
| --maxiter<br />-k |Maximum number of kmeans iteration. Only applicable to 2D algorithm. |10|
| --maxValue<br />-m |Clip VV and VH data to this maximum value, e.g. 0.1,0.5 for VV<0.1 and VH<0.5. If not set than wont clip. Only applicable to 2D algorithm. The default option will keep the original data (no clipping)|none|
| --skip-clustering<br />-s|    Do not perform clustering, assume output files are there. Useful when testing different strategies of picking flood classes.   |--|
| --strategy<br />-y | Strategy how to pick flood classes. Only applicable to 2D algorithm. Possible values: vh, vv, sum. |vv|
| --threshold<br />-n |Comma separated sequence of search space, start,end[,step], e.g.: 0.001,0.1,0.01 for 1D thresholding, or 2,10 for 2D clustering. |--|
| --conv-to-dB<br />-l |Convert linear power to dB (log scale) befor clustering. Only for the 2D algorithm. Recommended. |--|
| --fraction<br />-f |Fraction of pixels used to perform kmeans clustering. E.g. -f 0.1 for using 10% of data to identify clusters in k-means. Good for large rasters. Only applicable to 2D algorithm. |--|

Here is a comprehensive reference of available options for `mapper`.
| Option       |      Description      | Default|
|:-------------|:-------------|:-------------:|
| --base<br />-b |  use output from the 1D algorithm, provide polarization VV or VH |--|
| --auto<br />-a |  use automatically the best output from the 2D algorithm, do not use with `-c`, or `-f`|--|
| --classes<br />-c| use manually  output from the 2D algorithm, provide number of classes for mapping. Always use with `-f` |--|
| --floods<br />-f|  use manually  output from the 2D algorithm, provide number of flood classes. Always use with `-c` |--|


# Tips, FAQ

Q: My images are ZIP archives i.e. I have a bunch of .zip files in the folder. Will the program recognize them and load the rasters?
A: No. You have to unzip the files first. But this is easy, just enter the folder and type `unzip '*.zip'` (assuming you have unzip installed, if not - install it). Now you can feed the folder to floodsar.
