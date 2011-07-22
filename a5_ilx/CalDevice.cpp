
#include <Globals.h>
#include "CalDevice.h"

#include <stdio.h>
#include <assert.h>

#include "Memdebug.h"


/****
 * C++ object that ensures that Cal is initialized for the duration
 * of the application lifetime.
 *
 ***/

std::map<CALresource*,int> CalDevice::mRefs;
std::map<CALresource*,CalDevice*> CalDevice::mOwners;

class CalInitObject {
public:
    static bool check();

    CalInitObject();
    ~CalInitObject();

private:
    bool checkInt();
    bool mInitialized;
};

static CalInitObject gCalInitializer;

CalInitObject::CalInitObject() :
    mInitialized(false)
{
}

CalInitObject::~CalInitObject()
{
	/* causes crashes - why? */

	/*
    if (mInitialized) 
		calShutdown();
	*/
}

bool CalInitObject::check()
{
    return gCalInitializer.checkInt();
}

bool CalInitObject::checkInt()
{
    if (!mInitialized) {
        if (calInit() == CAL_RESULT_OK) {
            mInitialized = true;

			//fprintf(stderr, " [x] A5Il: Initialized CAL\n");

            CALuint version[3];
            calGetVersion(&version[0], &version[1], &version[2]);
            fprintf(stderr, " [x] A5Il: CAL Runtime version %d.%d.%d\n", version[0],
                   version[1], version[2]);

			// Query the number of devices on the system
			CALuint numDevices = 0;
			if(calDeviceGetCount(&numDevices) != CAL_RESULT_OK) 
				return false;

			for(unsigned int dev = 0; dev < numDevices; dev++)
			{
				// Get the information on the 0th device
				CALdeviceinfo info;
				if(calDeviceGetInfo(&info, dev) != CAL_RESULT_OK) 
					return false;

				char *devType = "unknown";
				
				switch(info.target)
				{
					case CAL_TARGET_600:
						devType = "GPU R600";
						break;
					case CAL_TARGET_610:
						devType = "GPU R610";
						break;
					case CAL_TARGET_630:
						devType = "GPU R630";
						break;
					case CAL_TARGET_670:
						devType = "GPU R670";
						break;
					case CAL_TARGET_7XX:
						devType = "GPU RV7xx";
						break;
					case CAL_TARGET_770:
						devType = "GPU RV770";
						break;
					case CAL_TARGET_710:
						devType = "GPU RV710";
						break;
					case CAL_TARGET_730:
						devType = "GPU RV730";
						break;
					case CAL_TARGET_CYPRESS:
						devType = "GPU CYPRESS";
						break;
					case CAL_TARGET_JUNIPER:
						devType = "GPU JUNIPER";
						break;
					case CAL_TARGET_REDWOOD:
						devType = "GPU REDWOOD";
						break;
					case CAL_TARGET_CEDAR:
						devType = "GPU CEDAR";
						break;
				}

				fprintf(stderr, " [x] A5Il:   [%i] Device Type = %s\n", dev, devType);
			}
        }
		else
		{
			fprintf(stderr, " [x] A5Il: Initializing CAL failed.\n");
        } 
    }
    return mInitialized;
}

CalDevice::CalDevice(int num) :
    mNumDev(num),
    mHasContext(false)
{
}

CalDevice::~CalDevice()
{
    /* All resources must be freed */
    if (mHasContext) {
        calCtxDestroy(mCtx);
    }
    assert( mResources.empty() );
    calDeviceClose(mDevice);
}

int CalDevice::getNumDevices()
{
    if (!CalInitObject::check()) return 0;
    CALuint numDevices = 0;
    
    if (calDeviceGetCount(&numDevices) != CAL_RESULT_OK) return 0;

    return (int)numDevices;
}

CalDevice* CalDevice::createDevice(int num)
{
    if (!CalInitObject::check()) return 0;
    CalDevice* pDev = new CalDevice(num);

    if(calDeviceOpen(&pDev->mDevice, num) != CAL_RESULT_OK) {
        /* Opening device failed */
        delete pDev;
        pDev = NULL;
        return pDev;
    }
    if (calCtxCreate( &pDev->mCtx, pDev->mDevice ) != CAL_RESULT_OK) {
        delete pDev;
        pDev = NULL;
        return pDev;
    }
    pDev->mHasContext = true;

    /* Retrieve info and attribs of the opened device */
    calDeviceGetInfo(&pDev->mInfo, num);
    pDev->mAttribs.struct_size = sizeof(CALdeviceattribs);
    if (calDeviceGetAttribs(&pDev->mAttribs, num)!= CAL_RESULT_OK) {
        fprintf(stderr, " [x] A5Il: ERROR: Could not query device attributes.\n");
    }

    return pDev;
}


/* Resource managment */
CALresource* CalDevice::resAllocLocal1D(int width, CALformat format,
                                        CALuint flags)
{
    CALresource newRes;
    if(calResAllocLocal1D(&newRes, mDevice, width, format, flags)
       != CAL_RESULT_OK) {
        fprintf(stderr, " [x] A5Il: ERROR: Could allocate 1D resource.\n");
        return NULL;
    }

    /* TODO: threadsafe */
    mResources.push_front(newRes);
    CALresource* pRes = &*(mResources.begin());
    mRefs[pRes] = 1;
    mOwners[pRes] = this;
    return pRes;
}

CALresource* CalDevice::resAllocLocal2D(int width, int height,
                                        CALformat format, CALuint flags)
{
    CALresource newRes;
    if(calResAllocLocal2D(&newRes, mDevice, width, height, format, flags)
       != CAL_RESULT_OK) {
        fprintf(stderr, " [x] A5Il: ERROR: Could allocate 2D resource.\n");
        return NULL;
    }

    /* TODO: threadsafe */
    mResources.push_front(newRes);
    CALresource* pRes = &*(mResources.begin());
    mRefs[pRes] = 1;
    mOwners[pRes] = this;
    return pRes;
}

CALresource* CalDevice::resAllocRemote1D(int width, CALformat format,
                                         CALuint flags)
{
    CALresource newRes;
    /* Memory will be specific to this device */
    if(calResAllocRemote1D(&newRes, &mDevice, 1, width, format, flags)
       != CAL_RESULT_OK) {
        fprintf(stderr, " [x] A5Il: ERROR: Could allocate 1D remote resource.\n");
        return NULL;
    }

    /* TODO: threadsafe */
    mResources.push_front(newRes);
    CALresource* pRes = &*(mResources.begin());
    mRefs[pRes] = 1;
    mOwners[pRes] = this;
    return pRes;
}

CALresource* CalDevice::resAllocRemote2D(int width, int height,
                                        CALformat format, CALuint flags)
{
    CALresource newRes;
    /* Memory will be specific to this device */
    if(calResAllocRemote2D(&newRes, &mDevice, 1, width, height, format, flags)
       != CAL_RESULT_OK) {
        fprintf(stderr, " [x] A5Il: ERROR: Could allocate 2D remote resource.\n");
        return NULL;
    }

    /* TODO: threadsafe */
    mResources.push_front(newRes);
    CALresource* pRes = &*(mResources.begin());
    mRefs[pRes] = 1;
    mOwners[pRes] = this;
    return pRes;
}

void CalDevice::unrefResource(CALresource* pRes)
{
    std::map<CALresource*,int>::iterator it = mRefs.find(pRes);
    if (it!=mRefs.end()) {
        int ref = (*it).second;
        ref = ref - 1;
        if (ref) {
            (*it).second = ref;
        } else {
            mRefs.erase(it);
            std::map<CALresource*,CalDevice*>::iterator it2 = mOwners.find(pRes);
            assert(it2!=mOwners.end());
            ((*it2).second)->freeResource(pRes);
            mOwners.erase(it2);
        }
    }
}

void CalDevice::refResource(CALresource* pRes)
{
    std::map<CALresource*,int>::iterator it = mRefs.find(pRes);
    if (it!=mRefs.end()) {
        (*it).second = (*it).second + 1;
    }
}

void CalDevice::freeResource(CALresource* res)
{
    std::list<CALresource>::iterator it = mResources.begin();
    while (it!=mResources.end()) {
        if (&(*it)==res) {
            calResFree(*it);
            mResources.erase(it);
            return;
        } 
        it++;
    }
}




