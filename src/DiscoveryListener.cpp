/*
 * DiscoveryListener
 * Aug 2014
 * nathan lachenmyer
 */

#ifdef TARGET_WIN32
#include "stdafx.h"
#endif

#include <memory>
#include "DiscoveryListener.h"
#include "DeviceHeader.h"

DiscoveryListener* DiscoveryListener::mDiscoveryService = NULL;

DiscoveryListener* DiscoveryListener::getInstance() {
  if(mDiscoveryService == NULL) {
    mDiscoveryService = new DiscoveryListener();
  }
  return mDiscoveryService;
}

void DiscoveryListener::freeInstance() {
  delete mDiscoveryService;
  mDiscoveryService = NULL;
}

int DiscoveryListener::getFrameLimit() {
  return mFrameLimit;
}

std::vector<std::shared_ptr<PixelPusher> > DiscoveryListener::getPushers() {
  mUpdateMutex.lock();
  std::vector<std::shared_ptr<PixelPusher> > pusherVector;
  for(std::map<std::string, std::shared_ptr<PixelPusher> >::iterator it = mPusherMap.begin();
	it != mPusherMap.end();
      ++it) {
    pusherVector.push_back(it->second);
  }
  mUpdateMutex.unlock();
  return pusherVector;
}

std::vector<std::shared_ptr<PixelPusher> > DiscoveryListener::getGroup(long groupId) {
  mUpdateMutex.lock();
  std::vector<std::shared_ptr<PixelPusher> > pusherVector;
  for(std::map<long, std::shared_ptr<PixelPusher> >::iterator it = mGroupMap.lower_bound(groupId);
	it != mGroupMap.upper_bound(groupId);
      ++it) {
    pusherVector.push_back(it->second);
  }
  mUpdateMutex.unlock();
  return pusherVector;
}

std::shared_ptr<PixelPusher> DiscoveryListener::getController(long groupId, long controllerId) {
  mUpdateMutex.lock();
  for(std::map<long, std::shared_ptr<PixelPusher> >::iterator it = mGroupMap.lower_bound(groupId);
	it != mGroupMap.upper_bound(groupId);
      ++it) {
    if(it->second->getControllerId() == controllerId) {
      mUpdateMutex.unlock();
      return it->second;
    }
  }
  //if no matching PixelPusher is found, return a nullPtr
  std::shared_ptr<PixelPusher> nullPtr;
  mUpdateMutex.unlock();
  return nullPtr;
}

DiscoveryListener::DiscoveryListener() {
	mUdpConnection = new ofxUDPManager();
	bool reuse_buff = 1;
	mUdpConnection->Create();
	mUdpConnection->Bind(mPort);
  ofLogNotice() << "Listening for UDP messages on port " << mPort;
  mMessageFlag = 0;
  mIncomingUdpMessage.resize(mIncomingPacketSize + 1);
  std::fill(mIncomingUdpMessage.begin(), mIncomingUdpMessage.end(), 0);
  mUdpMessage.resize(mIncomingPacketSize + 1);
  std::fill(mUdpMessage.begin(), mUdpMessage.end(), 0);
  
  mAutoThrottle = true;
  mFrameLimit = 60;

  mUpdateMapThread = std::thread(&DiscoveryListener::updatePusherMap, this);
}

DiscoveryListener::~DiscoveryListener() {
  if(mUpdateMapThread.joinable()) {
    mUpdateMapThread.join();
  }
}

void DiscoveryListener::update() {
  ofLogNotice() << "Updating registry...";
	/*------------NEED TO UPDATE-----------*/
  mUdpConnection->Receive(&mIncomingUdpMessage[0], mIncomingPacketSize);

  if(mIncomingPacketSize > 0) {
    mUpdateMutex.lock();
    mUdpMessage.clear();
    ofLogNotice() << "Received packet, processing...";
    std::copy(mIncomingUdpMessage.begin(), mIncomingUdpMessage.end(), mUdpMessage.begin());
    DeviceHeader* header;
    header = new DeviceHeader(&mUdpMessage[0], mIncomingPacketSize);
    if(header->getDeviceType() != PIXELPUSHER) {
      //if the device type isn't PixelPusher, end processing it right here.
      return;
    }
    
    std::shared_ptr<PixelPusher> incomingDevice(new PixelPusher(header));
    std::string macAddress = incomingDevice->getMacAddress();
    std::string ipAddress = incomingDevice->getIpAddress();
		mLastSeenMap[macAddress] = std::clock() / CLOCKS_PER_SEC;

    if(mPusherMap.count(macAddress) == 0) {
      //does not already exist in the map
      addNewPusher(macAddress, incomingDevice);
      ofLogNotice("", "Adding new PixelPusher %s at address %s", macAddress.c_str(), ipAddress.c_str());
    }
    else {
      //already exists in the map
      if(!mPusherMap[macAddress]->isEqual(incomingDevice)) {
	//if the pushers are not equal, replace it with this one
	updatePusher(macAddress, incomingDevice);
	ofLogNotice("", "Updating PixelPusher %s at address %s", macAddress.c_str(), ipAddress.c_str());
      }
      else {
	//if they're the same, then just update it
	mPusherMap[macAddress]->updateVariables(incomingDevice);
	ofLogNotice("", "Updating PixelPusher %s at address %s", macAddress.c_str(), ipAddress.c_str());
	if(incomingDevice->getDeltaSequence() > 3) {
	  mPusherMap[macAddress]->increaseExtraDelay(5);
	}
	if(incomingDevice->getDeltaSequence() < 1) {
	  mPusherMap[macAddress]->decreaseExtraDelay(1);
	}
      }
    }
    
    mUpdateMutex.unlock();
  }
}

void DiscoveryListener::addNewPusher(std::string macAddress, std::shared_ptr<PixelPusher> pusher) {
  mPusherMap.insert(std::make_pair(macAddress, pusher));
  mGroupMap.insert(std::make_pair(pusher->getGroupId(), pusher));
  pusher->createCardThread();
}

void DiscoveryListener::updatePusher(std::string macAddress, std::shared_ptr<PixelPusher> pusher) {
  mPusherMap[macAddress]->copyHeader(pusher);
}

void DiscoveryListener::updatePusherMap() {
  mRunUpdateMapThread = true;
  while(mRunUpdateMapThread) {
    mUpdateMutex.lock();
    for(std::map<std::string, std::shared_ptr<PixelPusher> >::iterator pusher = mPusherMap.begin(); pusher != mPusherMap.end();) {
			//pusher->first is Mac Address, pusher->second is the shared pointer to the PixelPusher
      if(!pusher->second->isAlive()) {
				ofLogNotice("", "DiscoveryListener removing PixelPusher %s from all maps.", pusher->first.c_str());
				pusher->second->destroyCardThread();
				//remove pusher from maps
				mLastSeenMap.erase(pusher->first);
				mPusherMap.erase(pusher++);
      }
      else {
				++pusher;
      }
    }
    mUpdateMutex.unlock();
    this_thread::sleep_for(std::chrono::milliseconds(1000));
  }  
}
