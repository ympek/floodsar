#pragma once

/* 
* 
* Utility script for representing coordinates in the algorothm
*
*/

class XYPair
{
public:
  XYPair(double x_, double y_)
    : x(x_)
    , y(y_)
  {
  }
  double x;
  double y;
};
