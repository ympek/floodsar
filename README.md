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
* Geotiff describing AOI (Area of interest). The program will crop all images to this area. Only the bounding box of the geotiff is needed, pixel values dont matter. If you have already cropped images, this step can be skipped.

## Usage / use cases

### Basic

If you have uncropped/unprepared SAR time series, the program will have to crop the images to area of interest first. If more than one image has the same date, the program will perform mosaicking of the files. There is also option of reprojecting the images implemented as a command line parameter (`-e`).

The SAR images time series should be stored in one directery (provided using the `-d` parameter), should have the same extension and a specified pattern of names as in [ASF RTC](https://hyp3-docs.asf.alaska.edu/guides/rtc_product_guide/#naming-convention) naming convention:

`S1x_yy_aaaaaaaaTbbbbbb_ppo_RTCzz_u_defklm_ssss_pol.extension`

where the key elements are:
* `aaaaaaaa`, which is the `YYYYMMDD` date of the image, eg. 20150121 for the 21st of January 2015.
* `pol`, which is a polarization of the image; either `VV` or `VH`.
* `extension`, whish is the image file extension, e.g, `tif`, specified using the `-e` parameter (default is `tif`).

In fact the program will look for the specified above keywords by splitting the file name at the places of the underscore character (`_`), therfore if your files do not come form the ASF RTC on demand processing just rename them to match the pattern above while keeping the appropite date, polarization and extension.

The .csv file (provided using the `-h` parameter) with the river gauge data should have two comma-separated columns without any headder (no colum names). The first column sould have a YYYYMMDD date of the observation and the second column should have a numerical value of the observation (water level or discharge), e.g:

```
20150121,1.2

20150122,1.4

20150123,1.5

20150124,1.2

[...]
```

There can be more observations in the csv file than SAR images. The program will match images with the csv file content by date.

The program will keep the pre-processed images in cache, therefor any subsequent runs should be executed with `--cache-only`, as we have to crop images only once for a given dataset. In this case the SAR images directoy path `-d` wont be required to run the program.

### Using manually pre-cropped imagery

If the SAR time series are already cropped to area of interest, we can inject cropped images right into programs' cache. Expected naming for images is:
`resampled__<POLARIZATION>_<DATE>`, where date is YYYYMMDD. Example: resampled__VH_20170228

Images should be put into the sub-folder `build/.floodsar-cache/cropped/` of the `floodsar` directory.

From now on, we can run the program with `--cache-only` option instead of providing the SAR images directoy with `-d`:

```
./floodsar --cache-only --hydro hydro.csv --algorithm 1D
```

or for 2D algo:

```
./floodsar --cache-only --hydro hydro.csv --algorithm 2D --strategy vh
```

## Command line options

Here is comprehesive reference of available options.

| Option       |      Description      | 
|-------------|:-------------|
| --algorithm |  Choose algorithm. Possible values: 1D, 2D. |
| --aoi | Area of Interest file path. The program expects geocoded tiff (GTiff). Content doesn't matter, the program just extracts the bounding box.
| --cache-only |    Do not process whole rasters, just use cropped images from cache.   |
| --directory | Path to directory which to search for SAR images |
| --extension | Files with this extension will be attempted to be loaded (.tif, .img or else. Leading dot is required) |
| --hydro | Path to file with hydrological data. Program expects two column csv - date, water elevation   |
| --skip-clustering |    Do not perform clustering, assume output files are there. Useful when testing different strategies of picking flood classes.   |
| --type | Data type, supported are poc, hype. Use hype if you process Sentinel-1 data. |
| --strategy | Strategy how to pick flood classes. Only applicable to 2D algorithm. Possible values: vh, vv, sum |
| --hydro | Path to file with hydrological data. Program expects two column csv - date, water elevation   |

```
./floodsar --cache-only --algorithm 1D
```

# Tips, FAQ

Q: My images are ZIP archives i.e. I have a bunch of .zip files in the folder. Will the program recognize them and load the rasters?
A: No. You have to unzip the files first. But this is easy, just enter the folder and type `unzip '*.zip'` (assuming you have unzip installed, if not - install it). Now you can feed the folder to floodsar.
