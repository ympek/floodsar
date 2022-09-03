set -euo pipefail

base_dir="$(pwd)"
build_dir="${base_dir}/build"
source_dir="${base_dir}/src"

mkdir -p ${build_dir}

pushd ${build_dir}

cmake -G "Ninja" ${source_dir}
cmake --build ${build_dir}

popd
