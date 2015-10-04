#ifdef TARGET_WIN32
#include "stdafx.h"
#endif

#include "ofLog.h"
#include "PixelPusher.h"
#include <algorithm>
#include <ctime>

PixelPusher::PixelPusher(DeviceHeader* header) {
  mArtnetUniverse = 0;
  mArtnetChannel = 0;
  mPort = 9897;
  mStripsAttached = 0;
  mPixelsPerStrip = 0;
  mExtraDelayMsec = 0;
  mMulticast = false;
  mMulticastPrimary = false;
  mAutothrottle = false;
  mSegments = 0;
  mPowerDomain = 0;
  mLastPingAt = std::clock() / CLOCKS_PER_SEC;
  mResetSentAt = std::clock() / CLOCKS_PER_SEC;
  mSendReset = false;

  mDeviceHeader = header;
  std::shared_ptr<unsigned char> packetRemainder = header->getPacketRemainder();
  int packetLength = header->getPacketRemainderLength();

  if(packetLength < 28) {
    ofLogError() << "Packet size is too small! PixelPusher can't be created.";
  }

  memcpy(&mStripsAttached, &packetRemainder.get()[0], 1);
  memcpy(&mMaxStripsPerPacket, &packetRemainder.get()[1], 1);
  memcpy(&mPixelsPerStrip, &packetRemainder.get()[2], 2);
  memcpy(&mUpdatePeriod, &packetRemainder.get()[4], 4);
  memcpy(&mPowerTotal, &packetRemainder.get()[8], 4);
  memcpy(&mDeltaSequence, &packetRemainder.get()[12], 4);
  memcpy(&mControllerId, &packetRemainder.get()[16], 4);
  memcpy(&mGroupId, &packetRemainder.get()[20], 4);
  memcpy(&mArtnetUniverse, &packetRemainder.get()[24], 2);
  memcpy(&mArtnetChannel, &packetRemainder.get()[26], 2);

  if(packetLength < 28 && header->getSoftwareRevision() > 100) {
    memcpy(&mPort, &packetRemainder.get()[28], 2);
  }
  else {
    mPort = 9897; 
  }
  short defaultNumberOfStrips = 8;
  short stripFlagSize = std::max(mStripsAttached, defaultNumberOfStrips);
  mStripFlags.resize(stripFlagSize);
  
  if (packetLength > 30 && header->getSoftwareRevision() > 108) {
    memcpy(&mStripFlags[0], &packetRemainder.get()[30], stripFlagSize);
  }
  else {
    for (int i = 0; i < stripFlagSize; i++) {
      mStripFlags[i] = 0;
    }
  }

  if (packetLength > 30 + stripFlagSize && header->getSoftwareRevision() > 108 ) {
    // set Pusher flags
    long pusherFlags;
    memcpy(&pusherFlags, &packetRemainder.get()[32+stripFlagSize], 4);
    setPusherFlags(pusherFlags);
    memcpy(&mSegments, &packetRemainder.get()[36+stripFlagSize], 4);
    memcpy(&mPowerDomain, &packetRemainder.get()[40+stripFlagSize], 4);
  }

  mPacket.reserve(400*mMaxStripsPerPacket); //too high, can be reduced
}

int PixelPusher::getNumberOfStrips() {
  return mStrips.size();
}

std::deque<std::shared_ptr<Strip> > PixelPusher::getStrips() {
  return mStrips;
}

std::deque<std::shared_ptr<Strip> > PixelPusher::getTouchedStrips() {
  std::deque<std::shared_ptr<Strip> > touchedStrips;
  for(auto strip : mStrips) {
    if(strip->isTouched()) {
      touchedStrips.push_back(strip);
    }
  }
  return touchedStrips;
}

void PixelPusher::addStrip(std::shared_ptr<Strip> strip) {
  mStrips.push_back(strip);
}

std::shared_ptr<Strip> PixelPusher::getStrip(int stripNumber) {
  return mStrips.at(stripNumber);
}

int PixelPusher::getMaxStripsPerPacket() {
  return mMaxStripsPerPacket;
}

int PixelPusher::getPixelsPerStrip(int stripNumber) {
  return mStrips.at(stripNumber)->getLength();
}

void PixelPusher::setStripValues(int stripNumber, unsigned char red, unsigned char green, unsigned char blue) {
  mStrips.at(stripNumber)->setPixels(red, green, blue);
}

void PixelPusher::setStripValues(int stripNumber, std::vector<std::shared_ptr<Pixel> > pixels) {
  mStrips.at(stripNumber)->setPixels(pixels);
}

std::string PixelPusher::getMacAddress() {
  return mDeviceHeader->getMacAddressString();
}

std::string PixelPusher::getIpAddress() {
  return mDeviceHeader->getIpAddressString();
}

void PixelPusher::sendPacket() {
  std::deque<std::shared_ptr<Strip> > remainingStrips = getTouchedStrips();
  bool payload = false;
  long packetLength = 0;
  mThreadDelay = 16.0;
  mPacket.clear();
  mRunCardThread = true;

  while(mRunCardThread) {
  if(getUpdatePeriod() > 100000.0) {
    mThreadDelay = (16.0 / (mStripsAttached / mMaxStripsPerPacket));
  }
  else if(getUpdatePeriod() > 1000.0) {
    mThreadDelay = (getUpdatePeriod() / 1000.0) + 1;
  }
  else {
    mThreadDelay = ((1000.0 / mFrameLimit) / (mStripsAttached / mMaxStripsPerPacket));
  }
  
  mTotalDelay = mThreadDelay + mThreadExtraDelay + mExtraDelayMsec;
  
  ofLogNotice("", "Total delay for PixelPusher %s is %ld", getMacAddress().c_str(), mTotalDelay);

  if(!mSendReset && remainingStrips.empty()) {
		this_thread::sleep_for(std::chrono::milliseconds(mTotalDelay));
  }

  /*
    else if (mSendReset) {
    sdfLog::logFormat("Resetting PixelPusher %s at %s", getMacAddress().c_str(), getIpAddress().c_str());
    // update Packet number
    memcpy(&resetCmdData[0], &mPacketNumber, 4);
    
    // send packet
    mUdpConnection->Send(resetCmdBuffer);
    mPacketNumber++;
    
    mSendReset = false;
    return;
  }
  */

  while(!remainingStrips.empty()) {
    ofLogNotice("", "Sending data to PixelPusher %s at %s:%d", getMacAddress().c_str(), getIpAddress().c_str(), mPort);
    payload = false;
    //packetLength = 0;
    //memcpy(&mPacket[0], &mPacketNumber, 4);
    //packetLength += 4;
    mPacket.clear();
    mPacket.push_back((mPacketNumber >> 24) & 0xFF);
    mPacket.push_back((mPacketNumber >> 16) & 0xFF);
    mPacket.push_back((mPacketNumber >> 8) & 0xFF);
    mPacket.push_back(mPacketNumber & 0xFF);
    
    for(int i = 0; i < mMaxStripsPerPacket; i++) {
      ofLogNotice("", "Packing strip %d of %hu...", i, mMaxStripsPerPacket);

      if(remainingStrips.empty()) {
				this_thread::sleep_for(std::chrono::milliseconds(mTotalDelay));
				continue;
      }
      
      std::shared_ptr<Strip> strip = remainingStrips.front();
      strip->serialize();
      unsigned char* stripData = strip->getPixelData();
      int stripDataLength = strip->getPixelDataLength();
      short stripNumber = strip->getStripNumber();
      //memcpy(&mPacket[packetLength], &stripNumber, 2);
      //packetLength += 2;
      mPacket.push_back((stripNumber >> 8) & 0xFF);
      mPacket.push_back(stripNumber & 0xFF);
	
      //memcpy(&mPacket[packetLength], &stripData, stripDataLength);
      //packetLength += stripDataLength;
      
      /**************************************************************
               This seems to be where all of my problems are :(
       *************************************************************/
      std::copy(strip->begin(), strip->end(), back_inserter(mPacket));
      //copy doesn't seem to add stuff to mPacket...
      payload = true;
      remainingStrips.pop_front();
    }
    
    if(payload) {
      ofLogNotice("", "Payload confirmed; sending packet of %lu bytes", mPacket.size());
      mPacketNumber++;
			mUdpConnection->Send(reinterpret_cast<char *>(mPacket.data()), mPacket.size());
      //mUdpConnection.Send(reinterpret_cast<char *>(mPacket.data()), mPacket.size());
      payload = false;
      this_thread::sleep_for(std::chrono::milliseconds(mTotalDelay));
    }
  }
  }

  ofLogNotice("", "Closing Card Thread for PixelPusher %s", getMacAddress().c_str());
}

void PixelPusher::setPusherFlags(long pusherFlags) {
  mPusherFlags = pusherFlags; 
}

long PixelPusher::getGroupId() {
  return mGroupId;
}

long PixelPusher::getControllerId() {
  return mControllerId;
}

long PixelPusher::getDeltaSequence() {
  return mDeltaSequence;
}

void PixelPusher::increaseExtraDelay(long delay) {
  mExtraDelayMsec += delay;
}

void PixelPusher::decreaseExtraDelay(long delay) {
  if (mExtraDelayMsec >= delay) {
    mExtraDelayMsec -= delay;
  }
  else {
    mExtraDelayMsec = 0;
  }
}

long PixelPusher::getExtraDelay() {
  return mExtraDelayMsec;
}

long PixelPusher::getUpdatePeriod() {
  return mUpdatePeriod;
}

short PixelPusher::getArtnetChannel() {
  return mArtnetChannel;
}

short PixelPusher::getArtnetUniverse() {
  return mArtnetUniverse;
}

short PixelPusher::getPort() {
  return mPort;
}

long PixelPusher::getPowerTotal() {
  return mPowerTotal;
}

long PixelPusher::getPowerDomain() {
  return mPowerDomain;
}

long PixelPusher::getSegments() {
  return mSegments;
}

long PixelPusher::getPusherFlags() {
  return mPusherFlags;
}

void PixelPusher::copyHeader(std::shared_ptr<PixelPusher> pusher) {
  mControllerId = pusher->mControllerId;
  mDeltaSequence = pusher->mDeltaSequence;
  mGroupId = pusher->mGroupId;
  mMaxStripsPerPacket = pusher->mMaxStripsPerPacket;
  mPowerTotal = pusher->mPowerTotal;
  mUpdatePeriod = pusher->mUpdatePeriod;
  mArtnetChannel = pusher->mArtnetChannel;
  mArtnetUniverse = pusher->mArtnetUniverse;
  mPort = pusher->mPort;
  setPusherFlags(pusher->getPusherFlags());
  mPowerDomain = pusher->mPowerDomain;
}

void PixelPusher::updateVariables(std::shared_ptr<PixelPusher> pusher) {
  mDeltaSequence = pusher->mDeltaSequence;
  mMaxStripsPerPacket = pusher->mMaxStripsPerPacket;
  mPowerTotal = pusher->mPowerTotal;
  mUpdatePeriod = pusher->mUpdatePeriod;
}

bool PixelPusher::isEqual(std::shared_ptr<PixelPusher> pusher) {
  long updatePeriodDifference = getUpdatePeriod() - pusher->getUpdatePeriod();
  if(abs(updatePeriodDifference) > 500) {
    return false;
  }

  //include check for color of strips
  
  if(getNumberOfStrips() != pusher->getNumberOfStrips()) {
    return false;
  }

  if(mArtnetChannel != pusher->getArtnetChannel() || mArtnetUniverse != pusher->getArtnetUniverse()) {
    return false;
  }

  if(mPort != pusher->getPort()) {
    return false;
  }

  long powerTotalDifference = mPowerTotal - pusher->getPowerTotal();
  if(abs(powerTotalDifference) > 10000) {
    return false;
  }

  if(getPowerDomain() != pusher->getPowerDomain()) {
    return false;
  }

  if(getSegments() != pusher->getSegments()) {
    return false;
  }

  if(getPusherFlags() != pusher->getPusherFlags()) {
    return false;
  }
}

bool PixelPusher::isAlive() {
  if((std::clock() / CLOCKS_PER_SEC - mLastPingAt) < mTimeoutTime) {
    return true;
  }
  else {
    return false;
  }
}

void PixelPusher::createStrips() {
  for(int i = 0; i < mStripsAttached; i++) {
    std::shared_ptr<Strip> newStrip(new Strip(i, mPixelsPerStrip));
    mStrips.push_back(newStrip);
  }
}

void PixelPusher::createCardThread() {
  createStrips();

	mUdpConnection = new ofxUDPManager();
	bool reuse_buff = 1;
	mUdpConnection->Create();
	mUdpConnection->Bind(mPort);

  ofLogNotice("", "Connected to PixelPusher %s on port %d", getIpAddress().c_str(), mPort);
  mPacketNumber = 0;
  mThreadExtraDelay = 0;
  mCardThread = std::thread(&PixelPusher::sendPacket, this);
}

void PixelPusher::destroyCardThread() {
  if(mCardThread.joinable()) {
    mCardThread.join();
  }
}
