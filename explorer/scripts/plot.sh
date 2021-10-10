#!/usr/bin/Rscript --vanilla
args = commandArgs(trailingOnly=TRUE)

numOfClasses = args[2]
data <- read.csv2(paste('./inputfiles/', args[1], '_cl_', numOfClasses, '.csv', sep=""), TRUE, ',')

# no nic musimy to wytestowac w R STudio...

# BIG BRAIN TIME.......
if (numOfClasses>=2) {
  class1 = data[data$CLASS == 1, ]
  class2 = data[data$CLASS == 2, ]
}

if (numOfClasses>=3) {
  class3 = data[data$CLASS == 3, ]
}

if (numOfClasses>=4) {
  class4 = data[data$CLASS == 4, ]
}

if (numOfClasses>=5) {
  class5 = data[data$CLASS == 5, ]
}

if (numOfClasses>=6) {
  class6 = data[data$CLASS == 6, ]
}

png(paste("./plots/", args[1], "_classes__", numOfClasses, ".png", sep=""))
plot(class1$VH, class1$VV, col=1, xlim=c(0,0.6), ylim=c(0,2))

if (numOfClasses>=2) {
  points(class2$VH, class2$VV, col=2)
}

if (numOfClasses>=3) {
  points(class3$VH, class3$VV, col=3)
}

if (numOfClasses>=4) {
  points(class4$VH, class4$VV, col=4)
}

if (numOfClasses>=5) {
  points(class5$VH, class5$VV, col=5)
}

if (numOfClasses>=6) {
  points(class6$VH, class6$VV, col=6)
}

dev.off()

