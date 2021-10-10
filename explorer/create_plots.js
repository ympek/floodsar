const fs = require("fs").promises;
const { execFile } = require('child_process');

var requestedPlots = [];
var workersNum = 4;

async function requestPlots() {
  // ehh to trzeba na outputs przerobic.
  // kurde to nie takie proste...
  const dir = await fs.opendir('./cache/kmeans_outputs');

  var outputs = [];
  for await (const dirent of dir) {
    var idxOf = dirent.name.indexOf('ONE_');
    if (idxOf != -1) {
      const pointsFile = 
    }
  }

  outputs.forEach(async classesnum => {
    // prepare input file and run R...
    const points = await fs.readFile("./cache/kmeans_inputs/input_" + dateid, "utf-8");
    const classes = await fs.readFile("./cache/kmeans_outputs/input_" + dateid + "_cl_" + classesnum + "/" + classesnum + "-points.txt", "utf-8");

    // const clusters = await fs.readFile("./cache/kmeans_outputs/input_" + dateid + "_cl_" + classesnum + "/" + classesnum + "-clusters.txt", "utf-8");

    const pointsArray = points.split("\n");
    const classesArray = classes.split("\n");

    let output = "VH,VV,CLASS\n";

    pointsArray.forEach(function (point, index) {
      const [vh, vv] = point.split(" ");
      if (!vh || !vv) return;
      output += vh;
      output += ",";
      output += vv;
      output += ",";
      output += classesArray[index];
      output += "\n";
    });

    await fs.writeFile('./scripts/inputfiles/' + dateid + '_cl_' + classesnum + '.csv', output, "utf-8");

    requestedPlots.push({ dateid: dateid, numClasses: classesnum });
  });
}

requestPlots()
  .then(nothing => {
    console.log("Let's wait now.");
  })
  .catch(err => {
    console.log("Err: ", err);
  });

setInterval(function() {
  if (requestedPlots.length === 0) {
    console.log("no plots requested");
    return;
  }
  console.log('co jest kurwa', requestedPlots.length, workersNum);
  const reqPlot = requestedPlots.shift();

  if (workersNum > 0) {
    console.log('plotting for...', reqPlot.dateid);
    workersNum--;
    var newProcess = execFile(__dirname + '/scripts/plot.sh', [ reqPlot.dateid, reqPlot.numClasses], {
      cwd: '/app/explorer/scripts/'
    },
      (error, stdout, stderr) => {
        console.log(reqPlot.dateid, reqPlot.numClasses, 'zakonczony');
        if (error) {
          console.log(error);
        }
        console.log(stdout);
        // io.emit('image available', {
        //   id: reqPlot.dateid,
        //   classes: reqPlot.numClasses,
        //   html: '<h2>' + prettyDateid(reqPlot.dateid) + '</h2><img src="/plotz/' + reqPlot.dateid + '.png">'
        // });
        workersNum++;
      });
  }
}, 100);
