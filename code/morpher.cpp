#include <OpenImageIO/imageio.h>
 #ifdef __APPLE__
 #  pragma clang diagnostic ignored "-Wdeprecated-declarations"
 #  include <GLUT/glut.h>
 #else
 #  include <GL/glut.h>
 #endif

#include "Image.h"

#include <stdio.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include "glm/vec2.hpp" // glm::vec2
#include "glm/gtx/transform.hpp"

#define SUCCESS_CODE 1
#define FAILURE_CODE 0

using namespace std;
OIIO_NAMESPACE_USING
using std::string;

using std::vector;
using glm::vec2;

enum ImageType {
  SOURCE = 5, DESTINATION = 6, MORPHED = 7
};

// all images will have the exact same height and width
Image *morphedImage = NULL;
Image *source = NULL;
Image *destination = NULL;
Image *toDisplay = NULL;

// buffer to store feature vectors
vector<vec2> sourceFeatureLines;
vector<vec2> destFeatureLines;

// image name strings
string sourceImage, destImage, morphedImageName;
string parameterFileName = "";
float a, b, p;
int frames;

int type;  // type - source or destination?

// always ask user for output image file name
void writeimage(string outfilename=""){

	if (!morphedImage)
		return;

	if (outfilename.empty()) {
	  cout << "enter output image filename: ";
	  cin >> outfilename;
	}

  // create the oiio file handler for the image
  ImageOutput *outfile = ImageOutput::create(outfilename);
  if(!outfile){
    cerr << "Could not create output image for " << outfilename << ", error = " << geterror() << endl;
    return;
  }

  // open a file for writing the image. The file header will indicate an image of
  // width w, height h, and 4 channels per pixel (RGBA). All channels will be of
  // type unsigned char
  ImageSpec spec(morphedImage->getWidth(), morphedImage->getHeight(), 4, TypeDesc::UINT8);

  if(!outfile->open(outfilename, spec)){
    cerr << "Could not open " << outfilename << ", error = " << geterror() << endl;
    ImageOutput::destroy(outfile);
    return;
  }

  // write the image to the file. All channel values in the pixmap are taken to be
  // unsigned chars
  if(!outfile->write_image(TypeDesc::UINT8, morphedImage->getPixmap())){
    cerr << "Could not write image to " << outfilename << ", error = " << geterror() << endl;
    ImageOutput::destroy(outfile);
    return;
  }

  // close the image file after the image is written
  if(!outfile->close()){
    cerr << "Could not close " << outfilename << ", error = " << geterror() << endl;
    ImageOutput::destroy(outfile);
    return;
  }

  // free up space associated with the oiio file handler
  ImageOutput::destroy(outfile);
}

int readimage(string name, Image **image) {

    // read the image
    ImageInput* input = ImageInput::open(name);
    if (! input)
        return FAILURE_CODE;

    const ImageSpec &spec = input->spec();
    // get the metadata for the image(dimensions and number of channels)
    int width = spec.width;
    int height = spec.height;
    int channels = spec.nchannels;

		// allocate space in memory to store the image data
    unsigned char pixmap[channels * width * height];

    if (!input->read_image(TypeDesc::UINT8, pixmap)) {
        cerr << "Could not read image " << name << ", error = " << geterror() << endl;
        ImageInput::destroy (input);
        return FAILURE_CODE;
    }
    // close the file handle
    if (!input->close()) {
      cerr << "Could not close " << name << ", error = " << geterror() << endl;
      ImageInput::destroy (input);
      return FAILURE_CODE;
    }

    ImageInput::destroy(input);

		// copy the pixmap into the image
    *image = new Image(width, height, channels);
    (*image)->copyImage(pixmap);   // make a deep copy of the pixmap

		return SUCCESS_CODE;  // the image was read successfully
}

/*
   Reshape Callback Routine: sets up the viewport and drawing coordinates
   This routine is called when the window is created and every time the window
   is resized, by the program or by the user
*/
void handleReshape(int w, int h) {

    // set the viewport to be the entire window
    glViewport(0, 0, w, h);

    // define the drawing coordinate system on the viewport
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
}

/*
  this is the main display routine
  using pixelZoom to always fit the image into the display window
  in the end, we flip the image so that it doesn't display upside down
*/
void drawImage() {

  if (toDisplay) {

    glClear(GL_COLOR_BUFFER_BIT);  // clear window to background color

    int width = toDisplay->getWidth();
    int height = toDisplay->getHeight();

    // flip the image so that we can see it straight
    Image* flipped = toDisplay->flip();
    glDrawPixels(width, height, GL_RGBA, GL_UNSIGNED_BYTE, flipped->getPixmap());
    flipped->destroy();
    delete flipped;

    glFlush();
  }
}

// strip the extension from the file name
string stripExtension(string fileName) {

  string extensions[3] = {".png", ".jpg", ".PNG"};

  int i = 0;  // current extension
  do {
      string extension = extensions[i];
      string name = fileName.substr(0, fileName.find(extension));

      if (name.compare(fileName) != 0)
          return name;

      i++;  // go to the next extension if the current one doesn't work
  }
  while (i < 3);

  return fileName;
}

// write feature points to the disk
void writeDatFiles() {

  string sourceDatName = stripExtension(sourceImage) + ".dat";
  string destDatName = stripExtension(destImage) + ".dat";

  ofstream sDatFile(sourceDatName);
  ofstream dDatFile(destDatName);

  if (sDatFile && dDatFile) {
    for (int i = 0; i < sourceFeatureLines.size(); ++i) {
        float x = sourceFeatureLines[i].x;
        float y = sourceFeatureLines[i].y;
        sDatFile << x << " " << y << "\n";
    }
    for (int i = 0; i < destFeatureLines.size(); ++i) {
        float x = destFeatureLines[i].x;
        float y = destFeatureLines[i].y;
        dDatFile << x << " " << y << "\n";
    }
  }
}

// merge and generate
void generateVectors(vector<Line> &sourceLines,
                    vector<Line> &destLines) {

  size_t sSize = sourceFeatureLines.size();
  size_t dSize = destFeatureLines.size();

  // assuming the sizes of both are even and equal
  assert(sSize % 2 == 0);
  assert(dSize % 2 == 0);
  assert(sSize == dSize);

  // merge
  // do not forget to change the coordinate system, turn top left into 0, 0
  for (size_t i = 0; i < sSize; i+=2) {
    sourceLines.push_back(Line(sourceFeatureLines[i], sourceFeatureLines[i+1]));
    destLines.push_back(Line(destFeatureLines[i], destFeatureLines[i+1]));
  }
}

// interpolate feature vectors from destination to source
void interpolate(const vector<Line> &sourceLines, const vector<Line> &destLines,
                 vector<Line> &interLines,
                 float alpha) {

  for (int i = 0; i < sourceLines.size(); ++i) {
    // from destination to source
    interLines[i].P = (1 - alpha) * destLines[i].P + alpha * sourceLines[i].P;
    interLines[i].Q = (1 - alpha) * destLines[i].Q + alpha * sourceLines[i].Q;
  }
}

// run the morphing algorithm
void runMorph() {

  // let the morphing begin
  cout << "Morphing process booting up...please wait...\n";
  vector<Line> sourceLines;
  vector<Line> destLines;

  // merge the collected points into vectors that actually represent the
  // feature lines
  generateVectors(sourceLines, destLines);

  // commit source and dest feature points to the disk
  writeDatFiles();

  // allocate some space for the interpolated lines
  vector<Line> interLines(destLines.size());

  Image *morphed = new Image(source->getWidth(), source->getHeight(), 4);

  // show an effect
  for (int i = 0; i < frames; ++i) {
    float alpha = i / (float)frames;
    // let the morphing begin
    interpolate(sourceLines, destLines, interLines, alpha);
    source->morph(destination, morphed, sourceLines, destLines, interLines,
                                            alpha, a, b, p);
    morphedImage = morphed;
    writeimage(morphedImageName + to_string(i+1) + ".png");
    cout << "Frame " << i+1 << " complete!\n";
  }

  // free space occupied by the temp morphed image
  morphed->destroy();
  delete morphed;

  cout << "Morphing complete!\n";
  toDisplay = destination;
}

/*
   This routine is called every time a key is pressed on the keyboard
*/
void handleKey(unsigned char key, int x, int y) {

  switch(key) {
    case 'w':
    case 'W':
				if (morphedImage)
        	writeimage(morphedImageName);
        break;
    case 's':
    case 'S':
        cout << "Number: " << destFeatureLines.size() << "\n";
        break;
    case 'p':
    case 'P':
        cout << "Number: " << sourceFeatureLines.size() << "\n";
        break;
    case 'd':
    case 'D':
        // done reading the feature lines of the image
        cout << "-------------\n";
        type += 1;
        // change the image
        if (type == DESTINATION) {
          cout << "Lines: " << sourceFeatureLines.size() << "\n";
          toDisplay = destination;
          glutPostRedisplay();
        }
        else if (type == MORPHED) {
          runMorph();
        }
        break;
    case 'q':		// q - quit
    case 'Q':
    case 27:		// esc - quit
        exit(0);
    default:		// not a valid key -- just ignore it
        return;
  }

}

// callback to handle mouse click events
void mouse(int button, int state, int x, int y)
{
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN)
	{
		// cursor position
    if (type == SOURCE) {
      cout << "(" << x << ", " << y << ")\n";
      sourceFeatureLines.push_back(vec2(x, y));
    }
    else if (type == DESTINATION) {
      cout << "(" << x << ", " << y << ")\n";
      destFeatureLines.push_back(vec2(x, y));
    }
	}
}

// read the dat file data into the buffers used for storing feature points
bool readDatFiles(string sourceDat, string destDat) {

  // try reading the files first
  ifstream sFile(sourceDat);
  ifstream dFile(destDat);

  // return if the file doesn't exist
  if (!sFile || !dFile)
    return 0;

  float x, y;
  while (sFile >> x >> y)
      sourceFeatureLines.push_back(vec2(x, y));

  // read the dest now
  while (dFile >> x >> y)
      destFeatureLines.push_back(vec2(x, y));

  return 1;
}

// read the parameter file
bool readParameters(string parameterFile) {

  ifstream pFile(parameterFile);

  if (!pFile)
    return 0;

  pFile >> a >> b >> p;

  return 1;
}

int main(int argc, char *argv[]){

  bool isDat = false;

  // check if the option is chosen
  if (string(argv[1]).compare("-d") == 0) {
      // read in the dat files
      sourceImage = argv[2];
      destImage = argv[3];

      string sourceDat = stripExtension(sourceImage) + ".dat";
      string destDat = stripExtension(destImage) + ".dat";

      // populate the vectors
      if (!readDatFiles(sourceDat, destDat)) {
        cout << "Couldn't read dat files\n";
        exit(1);
      }

      // read in the other args now
      sourceImage = argv[2];
      destImage = argv[3];
      morphedImageName = argv[4];
      frames = stoi(argv[5]);

      // optional argument check
      if (argc == 7) {
        // read in the parameters file as well
        if (!readParameters(argv[6])) {
          cout << "Couldn't read parameter files\n";
          exit(1);
        }
      }
      else {
        // ask the user for the parameters values
        cout << "Choose the parameter values\n";
        cout << "a: ";
        cin >> a;
        cout << "b: ";
        cin >> b;
        cout << "p: ";
        cin >> p;
      }
      // we have read data from the dat files
      isDat = true;
  }
  else {
      // read in the other args now
      sourceImage = argv[1];
      destImage = argv[2];
      morphedImageName = argv[3];
      frames = stoi(argv[4]);

      // optional argument check
      if (argc == 6) {
        // read in the parameters file as well
        if (!readParameters(argv[5])) {
          cout << "Couldn't read parameter files\n";
          exit(1);
        }
      }
      else {
        // ask the user for the parameters values
        cout << "Choose the parameter values\n";
        cout << "a: ";
        cin >> a;
        cout << "b: ";
        cin >> b;
        cout << "p: ";
        cin >> p;
      }
  }

  // read in the source and destination images
  int sourceStatus = readimage(sourceImage, &source);
  int destStatus = readimage(destImage, &destination);

  if (!sourceStatus || !destStatus) {
    cout << "Cannot read input images\n";
    exit(1);
  }

  // check if we need to specify feature vectors or not
  if (isDat) {
    // call morph directly without showing the display
    runMorph();
    exit(0);
  }

  // start up the glut utilities
  glutInit(&argc, argv);

  // create the graphics window, giving width, height, and title text
  glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
  glutCreateWindow("Morphing");

  // set up the callback routines to be called when glutMainLoop() detects
  // an event
  glutDisplayFunc(drawImage);	  // display callback
  glutKeyboardFunc(handleKey);	  // keyboard callback
  glutReshapeFunc(handleReshape); // window resize callback
  glutMouseFunc(mouse);

  type = SOURCE;
  toDisplay = source;
	glutReshapeWindow(toDisplay->getWidth(), toDisplay->getHeight());
	glutPostRedisplay();

	// Routine that loops forever looking for events. It calls the registered
  // callback routine to handle each event that is detected
  glutMainLoop();

	return 0;
}
