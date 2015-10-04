#ifdef TARGET_WIN32
#include "stdafx.h"
#endif

#include "Strip.h"

Strip::Strip(short stripNumber, int length) {
  for(int i = 0; i < length; i++) {
      mPixels.push_back(std::shared_ptr<Pixel>(new Pixel()));
  }
  mStripNumber = stripNumber;
  mTouched = false;
  mIsRGBOW = false;
  mPixelData.reserve(3*length);
  for(int i = 0; i < 3*length; i++) {
    mPixelData[i] = 0;
  }
  mPowerScale = 1.0;
}

Strip::~Strip() {
}

bool Strip::isRGBOW() {
  return mIsRGBOW;
}

void Strip::setRGBOW(bool rgbow) {
  //to implement -- not sure how exactly the conversion works
}

int Strip::getLength() {
  return mPixels.size();
}

bool Strip::isTouched() {
  return mTouched;
}

short Strip::getStripNumber() {
  return mStripNumber;
}

void Strip::setPixels(unsigned char r, unsigned char g, unsigned char b) {
  for(int i = 0; i < mPixels.size(); i++) {
    mPixels[i]->setColor(r, g, b);
  }
  mTouched = true;
}

void Strip::setPixels(std::vector<std::shared_ptr<Pixel> > pixels) {
  mPixels = pixels;
  mTouched = true;
}

void Strip::setPixel(int position, unsigned char r, unsigned char g, unsigned char b) {
  mPixels[position]->setColor(r,g,b);
  mTouched = true;
}

void Strip::setPixel(int position, std::shared_ptr<Pixel> pixel) {
  mPixels[position] = pixel;
  mTouched = true;
}

std::vector<std::shared_ptr<Pixel> > Strip::getPixels() {
  return mPixels;
}

int Strip::getNumPixels() {
  return mPixels.size();
}

void Strip::setPowerScale(double powerscale) {
  mPowerScale = powerscale;
}

void Strip::serialize() {
  for(int i = 0; i < mPixels.size(); i++) {
    mPixelData[3*i+0] = (unsigned char)(mPixels[i]->mRed * mPowerScale);
    mPixelData[3*i+1] = (unsigned char)(mPixels[i]->mGreen * mPowerScale);
    mPixelData[3*i+2] = (unsigned char)(mPixels[i]->mBlue * mPowerScale);
  }
  mTouched = false;
}

unsigned char* Strip::getPixelData() {
  return mPixelData.data();
}

int Strip::getPixelDataLength() {
  return mPixels.size();
}

std::vector<unsigned char>::iterator Strip::begin() {
  return mPixelData.begin();
}

std::vector<unsigned char>::iterator Strip::end() {
  return mPixelData.end();
}
