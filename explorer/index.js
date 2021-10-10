const express = require('express')
const port = 45678


const http = require('http');

const bodyParser = require('body-parser');
const fs = require("fs").promises;
const exphbs  = require('express-handlebars');
const app = express();
const server = http.createServer(app);
const GeoTIFF = require('geotiff');
const serveIndex = require('serve-index');

const { Server } = require("socket.io");
const io = new Server(server);

const { execFile } = require('child_process');

async function readTiff(path) {
  try {
    console.log('reading' + path);
    const rawData = await fs.readFile(path);
    console.log(rawData);
    return rawData;
  } catch(err) {
    console.error("dupa", error);
  }
}

app.engine('handlebars', exphbs());
app.set('view engine', 'handlebars');

app.use(bodyParser.urlencoded({ extended: false }));
app.use(express.static('static'));
app.use(express.static('cache'));
app.use(serveIndex('cache'));

app.get('/all', async (req, resp) => {
  const dir = await fs.opendir('./cache/kmeans_inputs');
  var myDates = [];
  for await (const dirent of dir) {
    var idxOf = dirent.name.indexOf('input_');
    if (idxOf != -1) {
      var pair = dirent.name.split("_");
      myDates.push(pair[1]);
    }
  }

  // sort dates before sending them over the wire

  resp.render('home', {
    title: 'Strona główna',
    dates: myDates.sort()
  });
});

app.get('/plotz/:imagename', async (req, resp) => {
  var fullpath = __dirname + '/scripts/plots/' + req.params.imagename;
  try {
    await fs.access(fullpath);
    resp.sendFile(fullpath);
  } catch {
    console.log(req.params.imagename, 'not ready');
  }
});

app.get('/get-plot/:dateid/:classesnum?', async (req, resp) => {
  resp.send("ok");
});

function cutLeadingZero(str) {
  if (str.charAt(0) === "0") {
    return str.charAt(1);
  } return str;
}

function prettyDateid(dateid) {
  // strip trailing 0 i odejmij 1
  const year  = dateid.substring(0, 4);
  const month = dateid.substring(4, 6);
  const day   = dateid.substring(6, 8);

  const dateObj = new Date();
  dateObj.setYear(year);
  dateObj.setMonth(cutLeadingZero(month));
  dateObj.setDate(cutLeadingZero(day));

  const polishMonth = dateObj.toLocaleString("pl-pl", { month: "long" });

  return day + " " + polishMonth + " " + year;
}

app.get('/file/:filename', async (req, resp) => {
  const name = req.params.filename;
  console.log(name);

  const rawTiff = await readTiff('../.floodsar-cache/cropped/' + name);
  // const rawTiff = await readTiff('../.floodsar-cache/cropped/resampled__VH_20210110');
  console.log("typeof", typeof(rawTiff));

  const tiff = await GeoTIFF.fromArrayBuffer(rawTiff.buffer);
  const image = await tiff.getImage();

  // metadata:
  const width = image.getWidth();
  const height = image.getHeight();
  const tileWidth = image.getTileWidth();
  const tileHeight = image.getTileHeight();
  const samplesPerPixel = image.getSamplesPerPixel();

  // when we are actually dealing with geo-data the following methods return
  // meaningful results:
  const origin = image.getOrigin();
  const resolution = image.getResolution();
  const bbox = image.getBoundingBox();

  // resp.json({
  //   width, height, tileWidth, tileHeight, samplesPerPixel, origin, resolution, bbox, bytes: rawTiff.buffer
  // });
  resp.end(Buffer.from(rawTiff.buffer, 'binary'));
});

io.on('connection', async (socket) => {
  console.log('a user connected');
  const dir = await fs.opendir('./scripts/plots');
  var images = [];
  for await (const dirent of dir) {
    var idxOf = dirent.name.indexOf('png');
    if (idxOf != -1) {
      images.push(dirent.name);
    }
  }

  images.forEach( (img) => {
    var dateid = img.split('_classes__')[0];
    var classez = img.split('_classes__')[1].split('.')[0];
    socket.emit('image available', {
      id: dateid,
      classes: classez,
      html: '<h2>' + prettyDateid(dateid) + '</h2><img src="/plotz/' + img + '">'
    });

  });
});

server.listen(port, () => {
  console.log(`listening at http://localhost:${port}`);

  // GeoTIFF.fromArrayBuffer(
})


