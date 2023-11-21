# Floodsar

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
For the complete reference of <options>, data, and use cases, pleas visit the [docs](docs/README.md) directory. More examples and links to sample data are available in the [example](example/README.md) directory.
