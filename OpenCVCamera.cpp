// STL Header
#include <sstream>

// OpenNI Header
#include "Driver/OniDriverAPI.h"
#include "XnLib.h"
#include "XnHash.h"
#include "XnEvent.h"

// OpenCV Header
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>


#define TEST_RESOLUTION_X 640
#define TEST_RESOLUTION_Y 480

class OpenCV_Color_Stream : public oni::driver::StreamBase
{
public:
	OpenCV_Color_Stream( int iDeviceId ) : oni::driver::StreamBase()
	{
		m_osEvent.Create(TRUE);
		m_sendCount = 0;
		m_frameId = 1;

		m_Camera.open(iDeviceId);
	}

	~OpenCV_Color_Stream()
	{
		stop();
	}

	OniStatus start()
	{
		xnOSCreateThread(threadFunc, this, &m_threadHandle);

		return ONI_STATUS_OK;
	}

	void stop()
	{
		m_running = false;
	}

	OniStatus SetVideoMode(OniVideoMode*) {return ONI_STATUS_NOT_IMPLEMENTED;}
	OniStatus GetVideoMode(OniVideoMode* pVideoMode)
	{
		pVideoMode->pixelFormat = ONI_PIXEL_FORMAT_DEPTH_1_MM;
		pVideoMode->fps = 30;
		pVideoMode->resolutionX = TEST_RESOLUTION_X;
		pVideoMode->resolutionY = TEST_RESOLUTION_Y;
		return ONI_STATUS_OK;
	}

	OniStatus getProperty(int propertyId, void* data, int* pDataSize)
	{
		if (propertyId == ONI_STREAM_PROPERTY_VIDEO_MODE)
		{
			if (*pDataSize != sizeof(OniVideoMode))
			{
				printf("Unexpected size: %d != %d\n", *pDataSize, sizeof(OniVideoMode));
				return ONI_STATUS_ERROR;
			}
			return GetVideoMode((OniVideoMode*)data);
		}

		return ONI_STATUS_NOT_IMPLEMENTED;
	}

	OniStatus setProperty(int propertyId, const void* data, int dataSize)
	{
		if (propertyId == ONI_STREAM_PROPERTY_VIDEO_MODE)
		{
			if (dataSize != sizeof(OniVideoMode))
			{
				printf("Unexpected size: %d != %d\n", dataSize, sizeof(OniVideoMode));
				return ONI_STATUS_ERROR;
			}
			return SetVideoMode((OniVideoMode*)data);
		}
		else if (propertyId == 666)
		{
			if (dataSize != sizeof(int))
			{
				printf("Unexpected size: %d != %d\n", dataSize, sizeof(int));
				return ONI_STATUS_ERROR;
			}

			// Increment the send count.
			m_cs.Lock();
			m_sendCount += *((int*)data);
			m_cs.Unlock();

			// Raise the OS event, to allow thread to start working.
			m_osEvent.Set();
		}

		return ONI_STATUS_NOT_IMPLEMENTED;
	}

	virtual int GetBytesPerPixel() { return sizeof(OniRGB888Pixel); }

	OniDriverFrame* AcquireFrame()
	{
		OniDriverFrame* pFrame = (OniDriverFrame*)xnOSCalloc(1, sizeof(OniDriverFrame));
		if (pFrame == NULL)
		{
			XN_ASSERT(FALSE);
			return NULL;
		}

		int dataSize = TEST_RESOLUTION_X * TEST_RESOLUTION_Y * GetBytesPerPixel();
		pFrame->frame.data = xnOSMallocAligned(dataSize, XN_DEFAULT_MEM_ALIGN);
		if (pFrame->frame.data == NULL)
		{
			XN_ASSERT(FALSE);
			return NULL;
		}

		pFrame->pDriverCookie = xnOSMalloc(sizeof(int));
		*((int*)pFrame->pDriverCookie) = 1;

		pFrame->frame.dataSize = dataSize;
		return pFrame;
	}

	void addRefToFrame(OniDriverFrame* pFrame)
	{
		++(*((int*)pFrame->pDriverCookie));
	}

	void releaseFrame(OniDriverFrame* pFrame)
	{
		if (0 == --(*((int*)pFrame->pDriverCookie)) )
		{
			xnOSFree(pFrame->pDriverCookie);
			xnOSFreeAligned(pFrame->frame.data);
			xnOSFree(pFrame);
		}
	}

protected:

	// Thread
	static XN_THREAD_PROC threadFunc(XN_THREAD_PARAM pThreadParam)
	{
		OpenCV_Color_Stream* pStream = (OpenCV_Color_Stream*)pThreadParam;
		pStream->m_running = true;

		while (pStream->m_running)
		{
			//pStream->m_osEvent.Wait(XN_WAIT_INFINITE);
			int count = 0;
			do 
			{
				// Get the current count.
				pStream->m_cs.Lock();
				count = pStream->m_sendCount;
				if (pStream->m_sendCount > 0)
				{
					pStream->m_sendCount--;
				}
				pStream->m_cs.Unlock();
				OniDriverFrame* pFrame = pStream->AcquireFrame();
				pStream->BuildFrame(&pFrame->frame);
				pStream->raiseNewFrame(pFrame);

			} while (count > 0);
		}

		XN_THREAD_PROC_RETURN(XN_STATUS_OK);
	}

	virtual int BuildFrame(OniFrame* pFrame)
	{
		// get new image
		cv::Mat mImg;
		m_Camera >> mImg;
		cv::cvtColor( mImg, m_FrameRBG, CV_BGR2RGB );
		memcpy( pFrame->data, m_FrameRBG.data, pFrame->dataSize );

		pFrame->frameIndex = m_frameId;

		pFrame->videoMode.pixelFormat = ONI_PIXEL_FORMAT_RGB888;
		pFrame->videoMode.resolutionX = TEST_RESOLUTION_X;
		pFrame->videoMode.resolutionY = TEST_RESOLUTION_Y;
		pFrame->videoMode.fps = 30;

		pFrame->width = TEST_RESOLUTION_X;
		pFrame->height = TEST_RESOLUTION_Y;

		pFrame->cropOriginX = pFrame->cropOriginY = 0;
		pFrame->croppingEnabled = FALSE;

		pFrame->sensorType = ONI_SENSOR_COLOR;
		pFrame->stride = TEST_RESOLUTION_X * sizeof(OniRGB888Pixel);
		pFrame->timestamp = m_frameId * 33000;
		m_frameId++;
		return 1;
	}

	int m_frameId;


	int singleRes(int x, int y) {return y*TEST_RESOLUTION_X+x;}

	bool m_running;
	int m_sendCount;

	XN_THREAD_HANDLE		m_threadHandle;
	xnl::CriticalSection	m_cs;
	xnl::OSEvent			m_osEvent;
	cv::VideoCapture		m_Camera;
	cv::Mat					m_FrameRBG;
};

class OpenCV_Camera_Device : public oni::driver::DeviceBase
{
public:
	OpenCV_Camera_Device(OniDeviceInfo* pInfo, oni::driver::DriverServices& driverServices) : m_pInfo(pInfo), m_driverServices(driverServices)
	{
		m_sensors[0].sensorType = ONI_SENSOR_COLOR;

		m_sensors[0].numSupportedVideoModes = 1;
		m_sensors[0].pSupportedVideoModes = XN_NEW_ARR(OniVideoMode, 1);
		m_sensors[0].pSupportedVideoModes[0].pixelFormat = ONI_PIXEL_FORMAT_RGB888;
		m_sensors[0].pSupportedVideoModes[0].fps = 30;
		m_sensors[0].pSupportedVideoModes[0].resolutionX = TEST_RESOLUTION_X;
		m_sensors[0].pSupportedVideoModes[0].resolutionY = TEST_RESOLUTION_Y;
	}

	OniDeviceInfo* GetInfo()
	{
		return m_pInfo;
	}

	OniStatus getSensorInfoList(OniSensorInfo** pSensors, int* numSensors)
	{
		*numSensors = 1;
		*pSensors = m_sensors;

		return ONI_STATUS_OK;
	}

	oni::driver::StreamBase* createStream(OniSensorType sensorType)
	{
		if (sensorType == ONI_SENSOR_COLOR)
		{
			OpenCV_Color_Stream* pImage = XN_NEW( OpenCV_Color_Stream, m_pInfo->usbProductId );
			return pImage;
		}

		m_driverServices.errorLoggerAppend( "OpenCV Can't create a stream of type %d", sensorType);
		return NULL;
	}

	void destroyStream(oni::driver::StreamBase* pStream)
	{
		XN_DELETE(pStream);
	}

	OniStatus  getProperty(int propertyId, void* data, int* pDataSize)
	{
		OniStatus rc = ONI_STATUS_OK;

		switch (propertyId)
		{
		case ONI_DEVICE_PROPERTY_DRIVER_VERSION:
			{
				if (*pDataSize == sizeof(OniVersion))
				{
					OniVersion* version = (OniVersion*)data;
					version->major = version->minor = version->maintenance = version->build = 2;
				}
				else
				{
					m_driverServices.errorLoggerAppend("Unexpected size: %d != %d\n", *pDataSize, sizeof(OniVersion));
					rc = ONI_STATUS_ERROR;
				}
			}
			break;
		default:
			m_driverServices.errorLoggerAppend("Unknown property: %d\n", propertyId);
			rc = ONI_STATUS_ERROR;
		}
		return rc;
	}
private:
	OpenCV_Camera_Device(const OpenCV_Camera_Device&);
	void operator=(const OpenCV_Camera_Device&);

	OniDeviceInfo* m_pInfo;
	OniSensorInfo m_sensors[1];
	oni::driver::DriverServices& m_driverServices;
};

class OpenCV_Camera_Driver : public oni::driver::DriverBase
{
public:
	OpenCV_Camera_Driver(OniDriverServices* pDriverServices) : DriverBase(pDriverServices)
	{}

	OniStatus initialize(	oni::driver::DeviceConnectedCallback connectedCallback,
							oni::driver::DeviceDisconnectedCallback disconnectedCallback,
							oni::driver::DeviceStateChangedCallback deviceStateChangedCallback,
							void* pCookie )
	{
		if( oni::driver::DriverBase::initialize(connectedCallback, disconnectedCallback, deviceStateChangedCallback, pCookie) == ONI_STATUS_OK )
		{
			int iCounter = 0;
			bool bTest = true;
			while( bTest )
			{
				cv::VideoCapture mCamera( iCounter );
				if( mCamera.isOpened() )
				{
					std::stringstream ss;
					ss << "\\OpenCV\\Camera\\" << ( iCounter + 1 );
					std::string sText = ss.str();

					OniDeviceInfo* pInfo = XN_NEW(OniDeviceInfo);
					xnOSStrCopy( pInfo->vendor, "OpenCV", ONI_MAX_STR);
					xnOSStrCopy( pInfo->name, sText.c_str(), ONI_MAX_STR);
					xnOSStrCopy( pInfo->uri, sText.c_str(), ONI_MAX_STR);
					pInfo->usbProductId = uint16_t( iCounter );
					m_devices[pInfo] = NULL;
					deviceConnected(pInfo);
					deviceStateChanged(pInfo, 0);

					++iCounter;
				}
				else
				{
					break;
				}
			}
		}
		return ONI_STATUS_OK;
	}

	virtual oni::driver::DeviceBase* deviceOpen(const char* uri)
	{
		for (xnl::Hash<OniDeviceInfo*, oni::driver::DeviceBase*>::Iterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter)
		{
			if (xnOSStrCmp(iter->Key()->uri, uri) == 0)
			{
				// Found
				if (iter->Value() != NULL)
				{
					// already using
					return iter->Value();
				}

				OpenCV_Camera_Device* pDevice = XN_NEW(OpenCV_Camera_Device, iter->Key(), getServices());
				iter->Value() = pDevice;
				return pDevice;
			}
		}

		getServices().errorLoggerAppend("Looking for '%s'", uri);
		return NULL;
	}

	virtual void deviceClose(oni::driver::DeviceBase* pDevice)
	{
		for (xnl::Hash<OniDeviceInfo*, oni::driver::DeviceBase*>::Iterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter)
		{
			if (iter->Value() == pDevice)
			{
				iter->Value() = NULL;
				XN_DELETE(pDevice);
				return;
			}
		}

		// not our device?!
		XN_ASSERT(FALSE);
	}

	virtual OniStatus tryDevice(const char* uri)
	{
		if (xnOSStrCmp(uri, "Test"))
		{
			return ONI_STATUS_ERROR;
		}


		OniDeviceInfo* pInfo = XN_NEW(OniDeviceInfo);
		xnOSStrCopy(pInfo->uri, uri, ONI_MAX_STR);
		xnOSStrCopy(pInfo->vendor, "Test", ONI_MAX_STR);
		m_devices[pInfo] = NULL;

		deviceConnected(pInfo);

		return ONI_STATUS_OK;
	}

	void shutdown() {}

protected:

	XN_THREAD_HANDLE m_threadHandle;

	xnl::Hash<OniDeviceInfo*, oni::driver::DeviceBase*> m_devices;
};

ONI_EXPORT_DRIVER(OpenCV_Camera_Driver);
