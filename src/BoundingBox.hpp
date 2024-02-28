#pragma once

/*
* Class representing analyzed bound box
*/

class BoundingBox
{
public:
  double upperLeftX;
  double upperLeftY;
  double lowerRightX;
  double lowerRightY;
  int widthInPixels;
  int heightInPixels;
};
