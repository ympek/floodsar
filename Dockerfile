FROM osgeo/gdal:ubuntu-small-latest
LABEL maintainer="sz@ympek.net"

# Note: this is development setup i.e contains language server and other shitz...

# basic tools
RUN apt update && apt install -q -y build-essential g++ make curl git silversearcher-ag sudo ctags ccls wget software-properties-common dirmngr

# setup R
# add the signing key (by Michael Rutter) for these repos
# To verify key, run gpg --show-keys /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc 
# Fingerprint: 298A3A825C0D65DFD57CBB651716619E084DAB9
RUN wget -qO- https://cloud.r-project.org/bin/linux/ubuntu/marutter_pubkey.asc | sudo tee -a /etc/apt/trusted.gpg.d/cran_ubuntu_key.asc
# add the R 4.0 repo from CRAN -- adjust 'focal' to 'groovy' or 'bionic' as needed
RUN add-apt-repository "deb https://cloud.r-project.org/bin/linux/ubuntu $(lsb_release -cs)-cran40/"
RUN apt install --no-install-recommends r-base
# install node 14 (LTS)
RUN curl -sL https://deb.nodesource.com/setup_14.x | sudo -E bash -
RUN apt install -q -y nodejs
RUN npm i -g yarn

WORKDIR /root/

RUN wget https://github.com/neovim/neovim/releases/download/v0.5.0/nvim.appimage
RUN chmod u+x nvim.appimage && ./nvim.appimage --appimage-extract

COPY ./start.sh .

RUN git clone https://github.com/ympek/dotfiles
RUN git config --global user.email "sz@ympek.net"
RUN git config --global user.name "ympek"
RUN mkdir -p .config/nvim
RUN mkdir -p /home/ympek/notes
RUN ln -s /root/dotfiles/init.vim .config/nvim/
RUN ln -s /root/dotfiles/coc-settings.json .config/nvim/

# install vim-plug
RUN sh -c 'curl -fLo "${XDG_DATA_HOME:-$HOME/.local/share}"/nvim/site/autoload/plug.vim --create-dirs https://raw.githubusercontent.com/junegunn/vim-plug/master/plug.vim'

WORKDIR /app

COPY . .

WORKDIR /app/explorer

RUN npm install

WORKDIR /app

RUN make
RUN make mapper

CMD ["/root/start.sh", ""]
