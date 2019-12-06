/*
  a structure that helps model a pixel in a more intuitive way
*/

#ifndef PIXEL_H
#define PIXEL_H

// model an rgba pixel
struct pixel {
    unsigned char r, g, b, a;

    pixel() {
      r = g = b = 0;
      a = 255;
    }

    pixel(unsigned char r, unsigned char g, unsigned char b, unsigned char a) : r(r), g(g), b(b), a(a) {}

    unsigned char& operator [](int i) {
        if (i == 0) return r;
        if (i == 1) return g;
        if (i == 2) return b;
        return a;
    }
};

#endif
