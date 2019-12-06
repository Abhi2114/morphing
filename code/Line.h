#ifndef Line_H
#define Line_H

#include "glm/vec2.hpp"

using glm::vec2;

struct Line {
    vec2 P, Q;

    Line() {P = vec2(0, 0); Q = vec2(0, 0);}

    Line(vec2 P, vec2 Q): P(P), Q(Q) { }
};

#endif
