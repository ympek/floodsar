FROM osgeo/gdal:ubuntu-small-latest
LABEL maintainer="sz@ympek.net"

# Note: this is development setup i.e contains language server and other shitz...

# basic tools
RUN apt update && apt install -q -y build-essential g++ make curl git silversearcher-ag sudo wget software-properties-common dirmngr

# setup R
# add the signing key (by Michael Rutter) for these repos
# To verify key, run gpg --show-keys /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc 
# Fingerprint: 298A3A825C0D65DFD57CBB651716619E084DAB9
RUN wget -qO- https://cloud.r-project.org/bin/linux/ubuntu/marutter_pubkey.asc | sudo tee -a /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc
# add the R 4.0 repo from CRAN -- adjust 'focal' to 'groovy' or 'bionic' as needed
RUN add-apt-repository "deb https://cloud.r-project.org/bin/linux/ubuntu $(lsb_release -cs)-cran40/"
RUN apt install -y --no-install-recommends r-base
# install node 14 (LTS)
RUN curl -sL https://deb.nodesource.com/setup_14.x | sudo -E bash -
RUN apt install -q -y nodejs
RUN npm i -g yarn

WORKDIR /root/

COPY ./start.sh .

WORKDIR /app

COPY . .

WORKDIR /app/explorer

RUN npm install

WORKDIR /app

RUN make
RUN make mapper

RUN mkdir -p .floodsar-cache/cropped

CMD ["/root/start.sh", ""]
