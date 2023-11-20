# Floodsar example

Make sure that Floodsar was built without errors. Please download the required file from the repository: https://doi.org/10.34808/6nfs-6q42 to the `example` directory and unzip it therein:

```
cd example
unzip california-repo.zip
```
the `ls` command should give the following output: `README.md  README.txt  aoi.tif  discharge.csv  images`

Now you can run Floodsar:

```../build/floodsar -a 2D -n 4,7 -f 0.1 -o aoi.tif -d images/ -p EPSG:32610 -h discharge.csv -k 100 -m 0.1,0.5 -l```

If the command above worked you can test with a broader search space, bigger sample, and more iterations. Also now you can cache with the `-c` option:

```../build/floodsar -a 2D -n 4,20 -f 0.2 -o aoi.tif -d images/ -p EPSG:32610 -h discharge.csv -k 1000 -m 0.1,0.5 -l```

This may take a while. When finished use mapper to create the results:

```../build/mapper -a```

Now run Floodsar with 1D algorithm and prepare output:

```
../build/floodsar -a 1D -n 0.001,0.1,0.01  -o aoi.tif -d images/ -p EPSG:32610 -h discharge.csv
../build/mapper -b VV
../build/mapper -b VH
```

You can test the performance of Floodsar with different parameters in the 2D algorithm or different search spaces in the 1D algorithm.
Note that in case of conflicts, files are overwritten in the `mapped` directory.

Some tiff preview programs do display the output images incorrectly. This is due to data range used in the output, i.e, 0 = no flood, 1 = flooding.
In such a case you can increase contrast or adjust the displayed data range in your software. Alternatively you can use GIS software, like QGIS
(https://qgis.org/en/site/index.html), which will allow you to display the data correctly and overlay it with other geographical information.
