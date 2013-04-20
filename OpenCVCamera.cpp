// STL Header
#include <array>
#include <vector>
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

inline OniVideoMode BuildMode( int W, int H, int FPS )
{
	OniVideoMode mMode;
	mMode.resolutionX = W;
	mMode.resolutionY = H;
	mMode.fps = FPS;

	return mMode;
}

class OpenCV_Color_Stream : public oni::driver::StreamBase
{
public:
	OpenCV_Color_Stream( int iDeviceId ) : oni::driver::StreamBase()
	{
		m_iFrameId = 0;
		m_Camera.open(iDeviceId);
		UpdateData();
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
		m_bRunning = false;
	}

	OniStatus SetVideoMode(OniVideoMode*){
		return ONI_STATUS_NOT_IMPLEMENTED;
	}

	OniStatus GetVideoMode(OniVideoMode* pVideoMode)
	{
		*pVideoMode = m_VideoMode;
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

		return ONI_STATUS_NOT_IMPLEMENTED;
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
		pStream->m_bRunning = true;

		while( pStream->m_bRunning )
		{
			pStream->UpdateData();
		}

		XN_THREAD_PROC_RETURN(XN_STATUS_OK);
	}

	void UpdateData()
	{
		#pragma region OpenCV Code
		// get new image
		cv::Mat mImg;
		m_Camera >> mImg;

		// convert image form BGR to RGB
		cv::cvtColor( mImg, m_FrameRBG, CV_BGR2RGB );
		#pragma endregion

		#pragma region Build OniDriverFrame
		// create frame
		OniDriverFrame* pFrame = (OniDriverFrame*)xnOSCalloc( 1, sizeof( OniDriverFrame ) );
		if( pFrame != NULL )
		{
			// create the buffer of image
			OniFrame& rFrame = pFrame->frame;
			rFrame.data = xnOSMallocAligned( m_uDataSize, XN_DEFAULT_MEM_ALIGN );
			if( rFrame.data != NULL )
			{
				// copy data from cv::Mat to OniDriverFrame
				rFrame.dataSize = m_uDataSize;
				memcpy( rFrame.data, m_FrameRBG.data, m_uDataSize );

				// update metadata
				rFrame.frameIndex		= ++m_iFrameId;
				rFrame.videoMode		= m_VideoMode;
				rFrame.width			= mImg.cols;
				rFrame.height			= mImg.rows;
				rFrame.cropOriginX		= rFrame.cropOriginY = 0;
				rFrame.croppingEnabled	= FALSE;
				rFrame.sensorType		= ONI_SENSOR_COLOR;
				rFrame.stride			= m_uStride;
				rFrame.timestamp		= m_iFrameId * 33000;

				// create reference counter
				pFrame->pDriverCookie = xnOSMalloc( sizeof( int ) );
				*((int*)pFrame->pDriverCookie) = 1;

				// raise new frame
				raiseNewFrame( pFrame );
			}
			else
			{
				XN_ASSERT(FALSE);
				return;
			}
		}
		else
		{
			XN_ASSERT( FALSE );
			return;
		}
		#pragma endregion
	}

	void UpdateVideoMode()
	{
		// get current mode
		m_VideoMode.resolutionX	= int( m_Camera.get( CV_CAP_PROP_FRAME_WIDTH ) );
		m_VideoMode.resolutionY	= int( m_Camera.get( CV_CAP_PROP_FRAME_HEIGHT ) );
		m_VideoMode.fps			= int( m_Camera.get( CV_CAP_PROP_FPS ) );
		m_VideoMode.pixelFormat	= ONI_PIXEL_FORMAT_RGB888;

		// precompute metadata
		m_uStride	= m_VideoMode.resolutionX * sizeof( OniRGB888Pixel );
		m_uDataSize	= m_uStride * m_VideoMode.resolutionY;
	}

protected:
	int		m_iFrameId;
	bool	m_bRunning;
	size_t	m_uDataSize;
	size_t	m_uStride;

	XN_THREAD_HANDLE		m_threadHandle;
	cv::VideoCapture		m_Camera;
	cv::Mat					m_FrameRBG;
	OniVideoMode			m_VideoMode;
};

class OpenCV_Camera_Device : public oni::driver::DeviceBase
{
public:
	OpenCV_Camera_Device(OniDeviceInfo* pInfo, const std::vector<OniVideoMode>& rTestMode, oni::driver::DriverServices& driverServices ) : m_pInfo(pInfo), m_driverServices(driverServices)
	{
		m_sensors[0].sensorType = ONI_SENSOR_COLOR;

		std::vector<OniVideoMode> vSupportedMode;
		cv::VideoCapture mCamera( pInfo->usbProductId );
		if( mCamera.isOpened() )
		{
			for( auto itMode = rTestMode.begin(); itMode != rTestMode.end(); ++ itMode )
			{
				if( mCamera.set( CV_CAP_PROP_FRAME_WIDTH, itMode->resolutionX ) &&
					mCamera.set( CV_CAP_PROP_FRAME_HEIGHT, itMode->resolutionY ) &&
					mCamera.set( CV_CAP_PROP_FPS, itMode->fps ) )
				{
					vSupportedMode.push_back( *itMode );
				}
			}

			m_sensors[0].numSupportedVideoModes = vSupportedMode.size();
			m_sensors[0].pSupportedVideoModes = XN_NEW_ARR(OniVideoMode, vSupportedMode.size());
			for( size_t i = 0; i < vSupportedMode.size(); ++ i )
			{
				m_sensors[0].pSupportedVideoModes[i] = vSupportedMode[i];
				m_sensors[0].pSupportedVideoModes[i].pixelFormat = ONI_PIXEL_FORMAT_RGB888;
			}
		}
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
	{
		m_bListDevice	= true;
		m_iMaxTestNum	= 10;
		m_sDeviceName	= "\\OpenCV\\Camera\\";
		m_sVendorName	= "OpenCV Camera by Heresy";

		m_vModeToTest.push_back( BuildMode( 720, 480, 30 ) );
		m_vModeToTest.push_back( BuildMode( 640, 480, 30 ) );
		m_vModeToTest.push_back( BuildMode( 320, 240, 30 ) );
	}

	OniStatus initialize(	oni::driver::DeviceConnectedCallback connectedCallback,
							oni::driver::DeviceDisconnectedCallback disconnectedCallback,
							oni::driver::DeviceStateChangedCallback deviceStateChangedCallback,
							void* pCookie )
	{
		if( oni::driver::DriverBase::initialize(connectedCallback, disconnectedCallback, deviceStateChangedCallback, pCookie) == ONI_STATUS_OK )
		{
			int iCounter = 0;
			while( iCounter < m_iMaxTestNum )
			{
				cv::VideoCapture mCamera( iCounter );
				if( mCamera.isOpened() )
				{
					std::stringstream ss;
					ss << m_sDeviceName << ( iCounter + 1 );
					std::string sText = ss.str();

					OniDeviceInfo* pInfo = XN_NEW(OniDeviceInfo);
					xnOSStrCopy( pInfo->vendor, m_sVendorName.c_str(), ONI_MAX_STR);
					xnOSStrCopy( pInfo->name, sText.c_str(), ONI_MAX_STR);
					xnOSStrCopy( pInfo->uri, sText.c_str(), ONI_MAX_STR);
					pInfo->usbProductId = uint16_t( iCounter );
					m_devices[pInfo] = NULL;
					if( m_bListDevice )
					{
						deviceConnected(pInfo);
						deviceStateChanged(pInfo, 0);
					}
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

				OpenCV_Camera_Device* pDevice = XN_NEW(OpenCV_Camera_Device, iter->Key(), m_vModeToTest, getServices());
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
		for (xnl::Hash<OniDeviceInfo*, oni::driver::DeviceBase*>::Iterator iter = m_devices.Begin(); iter != m_devices.End(); ++iter)
		{
			if (xnOSStrCmp(iter->Key()->uri, uri) == 0)
			{
				// Found
				if (iter->Value() == NULL)
				{
					deviceConnected( iter->Key() );
				}

				return ONI_STATUS_OK;
			}
		}
		return DriverBase::tryDevice(uri);
	}

	void shutdown() {}

protected:
	bool		m_bListDevice;
	int			m_iMaxTestNum;
	std::string	m_sDeviceName;
	std::string	m_sVendorName;
	std::vector<OniVideoMode>	m_vModeToTest;
	xnl::Hash<OniDeviceInfo*, oni::driver::DeviceBase*> m_devices;
};

ONI_EXPORT_DRIVER(OpenCV_Camera_Driver);
