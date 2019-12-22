#include "Image.h"
#include <algorithm>
#include <string.h>
#include <math.h>
#include <cmath>        // std::abs
#include <iostream>

using std::cout;

#include "glm/vec2.hpp" // glm::vec2
#include "glm/gtx/transform.hpp"

using std::min;
using std::max;
using std::floor;

Image::Image(int width, int height, int channels) :
width(width), height(height), channels(channels)
{
    int numbytes = 4 * width * height;  // always use 4 channels
    // allocate space for the pixmap
    pixmap = new unsigned char[numbytes];

    // setup the matrix access as well
    matrix = new unsigned char *[height];
    // set all the pointers appropriately
    matrix[0] = pixmap;
    for (int i = 1; i < height; ++i)
        matrix[i] = matrix[i - 1] + 4 * width;
}

// convert the input image to RGBA format if required
void Image::copyImage(const unsigned char *pixmap_) {
    // get the number of bytes to copy
    int numbytes = channels * width * height;

    if (channels == 1) {
        // greyscale image
        for (int i = 0, j = 0; i < numbytes; ++i, j += 4) {
            pixmap[j] = pixmap_[i];
            pixmap[j+1] = pixmap_[i];
            pixmap[j+2] = pixmap_[i];
            pixmap[j+3] = 255;
        }
    }
    else if (channels == 3) {
        // RGB image
        int alphas = 0;  // the number of alphas we have seen till now
        for (int i = 0; i < numbytes; ++i) {
            if ((i + alphas) % 4 == 3) {
                // we've seen an alpha
                pixmap[i + alphas] = 255;
                alphas++;
            }
            pixmap[i + alphas] = pixmap_[i];
        }
    }
    else
        memcpy(pixmap, pixmap_, numbytes);  // vanilla RGBA image, no need to do anything
}

// flip the image upside down for displaying
Image* Image::flip() {

    // flip the image for displaying
    Image *reversed = new Image(width, height, channels);

    // copy the image row by row, from bottom to top, which ends up
    // flipping it
    for (int h = 0; h < height; ++h) {
        for (int w = 0; w < width; ++w) {
            pixel pix = getpixel(height - h - 1, w);
            reversed->setpixel(h, w, pix);
        }
    }

    return reversed;
}

// bilinear interpolation of point (x, y) based on the pixel color values
pixel Image::sampleBilinear(float x, float y) {

  int col0 = floor(x);
  int col1 = col0 + 1;
  int row0 = floor(y);
  int row1 = row0 + 1;

  // get the 4 neighbors for the current pixel
  int pc0 = max(0, min(col0, width-1));
  int pc1 = min(pc0+1, width-1);
  int pr0 = max(0, min(row0, height-1));
  int pr1 = min(pr0+1, height-1);

  // get the color values for the 4 neighbor pixels
  pixel pix00 = getpixel(pr0, pc0);
  pixel pix01 = getpixel(pr0, pc1);
  pixel pix10 = getpixel(pr1, pc0);
  pixel pix11 = getpixel(pr1, pc1);

  pixel result;

  // perform the bilinear interpolation
  for (int c = 0; c < 4; ++c) {
    double res = 0;

    res += ((double)pix00[c]) * (col1 - x) * (row1 - y);
    res += ((double)pix01[c]) * (col1 - x) * (y - row0);
    res += ((double)pix10[c]) * (x - col0) * (row1 - y);
    res += ((double)pix11[c]) * (x - col0) * (y - row0);

    // do not forget to clamp the values in case they go out of the range
    result[c] = min(255, max(0, (int)floor(res)));
  }

  return result;
}

// map point (w, h) wrt to interLines to a new point 'src' wrt to the sourceLines
vec2 warp(int w, int h,
          std::vector<Line> &sourceLines,
          std::vector<Line> &interLines,
          int a, int p, int b) {

  vec2 src;
  vec2 input(w, h);  // the input point, wrt to the interLine
  float X, Y;        // output point coordinates, wrt to the sourceLine

  // store the weighted sum for the corresponding point in the source image
  float sum_x = 0;
  float sum_y = 0;
  float weightSum = 0;

  // tail = P
  // head = Q
  for (int i = 0; i < sourceLines.size(); i++) {
	  
		vec2 pd = input - interLines[i].P;
		vec2 pq = interLines[i].Q - interLines[i].P;
		// get the length of the interpolated feature segment PQ
		float interLength = glm::length(pq);
		float u = glm::dot(pd, pq) / (interLength * interLength);

		// cross product between pd and pq
		float v = (pd.x * pq.y - pd.y * pq.x) / interLength;

		pq = sourceLines[i].Q - sourceLines[i].P;

		 // get the length of the corresponding vector P'Q' in the source
		float srcLength = glm::length(pq);
		// corresponding point based on the current line
		X = sourceLines[i].P.x + u * pq.x + v * pq.y / srcLength;
		Y = sourceLines[i].P.y + u * pq.y - v * pq.x / srcLength;

		// the sortest distance from the corresponding point to the line P'Q'
		float dist;
		if (u < 0)
		  dist = glm::length(pd);
		else if (u > 1)
		  dist = glm::length(input - interLines[i].Q);
		else
		  dist = abs(v);

		// perform a partial weighted sum
		float weight = pow(pow(interLength, p) / (a + dist), b);
		sum_x += X * weight;
		sum_y += Y * weight;
		weightSum += weight;
	}

  // average the computed sum values and return
  src.x = sum_x / weightSum;
  src.y = sum_y / weightSum;

  return src;
}

void Image::morph(Image *destination,
                  Image *morphed,
                  std::vector<Line> &sourceLines,
                  std::vector<Line> &destLines,
                  std::vector<Line> &interLines,
                  float alpha,
                  float a,
                  float b,
                  float p) {

  // source = *this

  for (int h = 0; h < height; ++h) {
    for (int w = 0; w < width; ++w) {
      // for each pixel
      vec2 out1 = warp(w, h, sourceLines, interLines, a, p, b);
      vec2 out2 = warp(w, h, destLines, interLines, a, p, b);

      // bilinear interpolation of the color values
      pixel sPixel = sampleBilinear(out1.x, out1.y);
      pixel dPixel = destination->sampleBilinear(out2.x, out2.y);

      pixel blend;

      // the good ol over operator applied to blend the two pixels together
      blend.r = alpha * sPixel.r + (1-alpha) * dPixel.r;
      blend.g = alpha * sPixel.g + (1-alpha) * dPixel.g;
      blend.b = alpha * sPixel.b + (1-alpha) * dPixel.b;
      blend.a = alpha * sPixel.a + (1-alpha) * dPixel.a;

      // set the new value
      morphed->setpixel(h, w, blend);
    }
  }
}
