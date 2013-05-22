// C Header
#include <string.h>

// STL Header
#include <fstream>
#include <iostream>
#include <map>
#include <vector>
#include <set>
#include <sstream>
#include <thread>

// OpenNI Header
#include "Driver/OniDriverAPI.h"
#include "XnLib.h"

// OpenCV Header
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

/**
 * Function to create a OniVideoMode object faster
 */
inline OniVideoMode BuildMode( int W, int H, int FPS )
{
	OniVideoMode mMode;
	mMode.resolutionX = W;
	mMode.resolutionY = H;
	mMode.fps = FPS;

	return mMode;
}

/**
 * Build OniVideoMode from input stream, format: 320/240@30
 */
std::istream& operator>>( std::istream& rIS, OniVideoMode& rMode )
{
	std::string sValue;
	rIS >> sValue;

	size_t	uPos1 = sValue.find_first_of( "/" ),
			uPos2 = sValue.find_first_of( "@" );
	if( uPos1 != std::string::npos && uPos2 != std::string::npos )
	{
		std::stringstream( sValue.substr( 0, uPos1 ) ) >> rMode.resolutionX;
		std::stringstream( sValue.substr( uPos1 + 1, uPos2 - uPos1 ) ) >> rMode.resolutionY;
		std::stringstream( sValue.substr( uPos2 + 1 ) ) >> rMode.fps;
	}
	return rIS;
}

/**
 * Comparsion of OniVideoMode, used for std::set<>
 */
bool operator<( const OniVideoMode& m1, const OniVideoMode& m2 )
{
	if( m1.resolutionX < m2.resolutionX )
		return true;
	else if( m1.resolutionX > m2.resolutionX )
		return false;
	
	if( m1.resolutionY < m2.resolutionY )
		return true;
	else if( m1.resolutionY > m2.resolutionY )
		return false;

	if( m1.fps < m2.fps )
		return true;
	else if( m1.fps > m2.fps )
		return false;

	return false;
}

/**
 * Color Sensor VideoStream
 */
class OpenCV_Color_Stream : public oni::driver::StreamBase
{
public:
	/**
	 * Constructor
	 */
	OpenCV_Color_Stream( int iDeviceId, oni::driver::DriverServices& driverServices ) : oni::driver::StreamBase(), m_driverServices(driverServices)
	{
		m_iFrameId		= 0;
		m_bMirroring	= false;
		m_pThread		= NULL;
		m_Camera.open( iDeviceId );
		UpdateVideoMode();
	}

	/**
	 * Destructor
	 */
	~OpenCV_Color_Stream()
	{
		stop();
	}

	/**
	 * Start load image
	 */
	OniStatus start()
	{
		if( m_Camera.isOpened() )
		{
			m_pThread = new std::thread( threadFunc, this );
			return ONI_STATUS_OK;
		}
		std::cerr << "The OpenCV camera is not opened." << std::endl;
		return ONI_STATUS_ERROR;
	}

	/**
	 * Stop load image
	 */
	void stop()
	{
		m_bRunning = false;
		if( m_pThread != NULL )
		{
			m_pThread->join();
			delete m_pThread;
			m_pThread = NULL;
		}
	}

	/**
	 * Check if the property is supported
	 */
	OniBool isPropertySupported( int propertyId )
	{
		switch( propertyId )
		{
			case ONI_STREAM_PROPERTY_VIDEO_MODE:
			case ONI_STREAM_PROPERTY_MIRRORING:
				return true;
		}
		return true;
	}

	/**
	 * get property
	 */
	OniStatus getProperty( int propertyId, void* data, int* pDataSize )
	{
		switch( propertyId )
		{
		case ONI_STREAM_PROPERTY_VIDEO_MODE:
			if( *pDataSize == sizeof(OniVideoMode) )
			{
				(*(OniVideoMode*)data) = m_VideoMode;
				return ONI_STATUS_OK;
			}
			else
			{
				m_driverServices.errorLoggerAppend( "Unexpected size: %d != %d\n", *pDataSize, sizeof(OniVideoMode) );
				return ONI_STATUS_ERROR;
			}
			break;

		case ONI_STREAM_PROPERTY_MIRRORING:
			if( *pDataSize == sizeof(OniBool) )
			{
				*((OniBool*)data) = m_bMirroring;
				return ONI_STATUS_OK;
			}
			else
			{
				m_driverServices.errorLoggerAppend( "Unexpected size: %d != %d\n", *pDataSize, sizeof(OniBool) );
				return ONI_STATUS_ERROR;
			}
			break;

		default:
			return ONI_STATUS_NOT_IMPLEMENTED;
		}
	}

	/**
	 * set property
	 */
	OniStatus setProperty(int propertyId, const void* data, int dataSize)
	{
		switch( propertyId )
		{
		case ONI_STREAM_PROPERTY_VIDEO_MODE:
			if (dataSize == sizeof(OniVideoMode))
			{
				OniVideoMode& rMode = *(OniVideoMode*)data;
				if( m_Camera.set( CV_CAP_PROP_FRAME_WIDTH, rMode.resolutionX ) &&
					m_Camera.set( CV_CAP_PROP_FRAME_HEIGHT, rMode.resolutionY ) &&
					m_Camera.set( CV_CAP_PROP_FPS, rMode.fps ) )
				{
					UpdateVideoMode();
					return ONI_STATUS_OK;
				}
				else
				{
					return ONI_STATUS_ERROR;
				}
			}
			else
			{
				m_driverServices.errorLoggerAppend( "Unexpected size: %d != %d\n", dataSize, sizeof(OniVideoMode) );
				return ONI_STATUS_ERROR;
			}
			break;

		case ONI_STREAM_PROPERTY_MIRRORING:
			if( dataSize == sizeof(OniBool) )
			{
				m_bMirroring = *((OniBool*)data);
				return ONI_STATUS_OK;
			}
			else
			{
				m_driverServices.errorLoggerAppend( "Unexpected size: %d != %d\n", dataSize, sizeof(OniBool) );
				return ONI_STATUS_ERROR;
			}
			break;

		default:
			return ONI_STATUS_NOT_IMPLEMENTED;
		}
	}

protected:

	// Thread function to update image
	static void threadFunc( OpenCV_Color_Stream* pStream )
	{
		pStream->m_bRunning = true;

		while( pStream->m_bRunning )
		{
			pStream->UpdateData();
		}
	}

	/**
	 * Update image
	 */
	void UpdateData()
	{
		#pragma region OpenCV Code
		// get new image
		cv::Mat mImg, m_FrameRBG;
		m_Camera >> mImg;

		// convert image form BGR to RGB
		cv::cvtColor( mImg, m_FrameRBG, CV_BGR2RGB );

		// mirror
		if( m_bMirroring )
			cv::flip( m_FrameRBG, m_FrameRBG, 1 );
		#pragma endregion

		#pragma region Build OniDriverFrame
		// create frame
		OniFrame* pFrame = getServices().acquireFrame();
		if( pFrame != NULL )
		{
			// create the buffer of image
			pFrame->data = new unsigned char[m_uDataSize];
			if( pFrame->data != NULL )
			{
				// copy data from cv::Mat to OniDriverFrame
				pFrame->dataSize = int(m_uDataSize);
				memcpy( pFrame->data, m_FrameRBG.data, m_uDataSize );

				// update metadata
				pFrame->frameIndex		= ++m_iFrameId;
				pFrame->videoMode		= m_VideoMode;
				pFrame->width			= mImg.cols;
				pFrame->height			= mImg.rows;
				pFrame->cropOriginX		= pFrame->cropOriginY = 0;
				pFrame->croppingEnabled	= FALSE;
				pFrame->sensorType		= ONI_SENSOR_COLOR;
				pFrame->stride			= int(m_uStride);
				pFrame->timestamp		= m_iFrameId * 3300;

				// raise new frame
				raiseNewFrame( pFrame );
				return;
			}
		}
		std::cerr << "Data allocate failed" << std::endl;
		#pragma endregion
	}

	/**
	 * Update VideoMode and compute some information
	 */
	void UpdateVideoMode()
	{
		// get current mode
		m_VideoMode.resolutionX	= int( m_Camera.get( CV_CAP_PROP_FRAME_WIDTH ) );
		m_VideoMode.resolutionY	= int( m_Camera.get( CV_CAP_PROP_FRAME_HEIGHT ) );
		m_VideoMode.fps			= int( m_Camera.get( CV_CAP_PROP_FPS ) );
		m_VideoMode.pixelFormat	= ONI_PIXEL_FORMAT_RGB888;

		// pre-compute metadata
		m_uStride	= m_VideoMode.resolutionX * sizeof( OniRGB888Pixel );
		m_uDataSize	= m_uStride * m_VideoMode.resolutionY;
	}

protected:
	int		m_iFrameId;
	bool	m_bRunning;
	OniBool	m_bMirroring;
	size_t	m_uDataSize;
	size_t	m_uStride;

	std::thread*		m_pThread;
	cv::VideoCapture	m_Camera;
	OniVideoMode		m_VideoMode;

	oni::driver::DriverServices&	m_driverServices;

private:
	OpenCV_Color_Stream( const OpenCV_Color_Stream& );
	void operator=( const OpenCV_Color_Stream& );
};

/**
 * Device
 */
class OpenCV_Camera_Device : public oni::driver::DeviceBase
{
public:
	/**
	 * Constructor
	 */
	OpenCV_Camera_Device( OniDeviceInfo* pInfo, const std::vector<OniVideoMode>& rTestMode, oni::driver::DriverServices& driverServices ) : m_pInfo(pInfo), m_driverServices(driverServices)
	{
		m_bCreated = false;
		m_sensors[0].sensorType = ONI_SENSOR_COLOR;

		// check if the given VideoMode is supported
		std::set<OniVideoMode> vSupportedMode;
		cv::VideoCapture mCamera( pInfo->usbProductId );
		if( mCamera.isOpened() )
		{
			// get default camera mode
			OniVideoMode mMode;
			mMode.pixelFormat	= ONI_PIXEL_FORMAT_RGB888;
			mMode.resolutionX	= int( mCamera.get( CV_CAP_PROP_FRAME_WIDTH ) );
			mMode.resolutionY	= int( mCamera.get( CV_CAP_PROP_FRAME_HEIGHT ) );
			mMode.fps			= int( mCamera.get( CV_CAP_PROP_FPS ) );

			// I don't know why, but OpenCV return 0 for FPS in my computer
			if( mMode.fps == 0 )
				mMode.fps = 30;

			vSupportedMode.insert( mMode );

			// test modes
			for( auto itMode = rTestMode.begin(); itMode != rTestMode.end(); ++ itMode )
			{
				if( mCamera.set( CV_CAP_PROP_FRAME_WIDTH, itMode->resolutionX ) &&
					mCamera.set( CV_CAP_PROP_FRAME_HEIGHT, itMode->resolutionY ) &&
					mCamera.set( CV_CAP_PROP_FPS, itMode->fps ) )
				{
					if( itMode->resolutionX == int( mCamera.get( CV_CAP_PROP_FRAME_WIDTH ) ) &&
						itMode->resolutionY == int( mCamera.get( CV_CAP_PROP_FRAME_HEIGHT ) ) &&
						itMode->fps == int( mCamera.get( CV_CAP_PROP_FPS ) ) )
					{
						vSupportedMode.insert( *itMode );
					}
				}
			}

			mCamera.release();

			// Set OpenNI data
			m_sensors[0].numSupportedVideoModes = int(vSupportedMode.size());
			m_sensors[0].pSupportedVideoModes = new OniVideoMode[ vSupportedMode.size() ];
			int	iIdx = 0;
			for( auto itMode = vSupportedMode.begin(); itMode != vSupportedMode.end(); ++ itMode )
			{
				m_sensors[0].pSupportedVideoModes[iIdx] = *itMode;
				m_sensors[0].pSupportedVideoModes[iIdx].pixelFormat = ONI_PIXEL_FORMAT_RGB888;
				++ iIdx;
			}

			m_bCreated = true;
		}
		else
		{
			m_driverServices.errorLoggerAppend( "Can't open OpenCV camera %d", pInfo->usbProductId );
		}
	}

	/**
	 * Destructor
	 */
	~OpenCV_Camera_Device(){}

	/**
	 * getSensorInfoList
	 */
	OniStatus getSensorInfoList( OniSensorInfo** pSensors, int* numSensors )
	{
		*numSensors = 1;
		*pSensors = m_sensors;

		return ONI_STATUS_OK;
	}

	/**
	 * create Stream
	 */
	oni::driver::StreamBase* createStream( OniSensorType sensorType )
	{
		if (sensorType == ONI_SENSOR_COLOR)
		{
			OpenCV_Color_Stream* pImage = new OpenCV_Color_Stream( m_pInfo->usbProductId, m_driverServices );
			return pImage;
		}

		m_driverServices.errorLoggerAppend( "OpenCV only provide color sensor" );
		return NULL;
	}

	/**
	 * destroy Stream
	 */
	void destroyStream( oni::driver::StreamBase* pStream )
	{
		delete pStream;
	}

	/**
	 * get Property
	 */
	OniStatus getProperty( int propertyId, void* data, int* pDataSize )
	{
		switch (propertyId)
		{
		case ONI_DEVICE_PROPERTY_DRIVER_VERSION:
			{
				if( *pDataSize == sizeof(OniVersion) )
				{
					OniVersion* version = (OniVersion*)data;
					version->major			= 0;
					version->minor			= 3;
					version->maintenance		= 0;
					version->build			= 0;
					return ONI_STATUS_OK;
				}
				else
				{
					m_driverServices.errorLoggerAppend( "Unexpected size: %d != %d\n", *pDataSize, sizeof(OniVersion) );
					return ONI_STATUS_ERROR;
				}
			}
			break;

		default:
			m_driverServices.errorLoggerAppend( "Unknown property: %d\n", propertyId );
			return ONI_STATUS_ERROR;
		}
	}

	/**
	 * make sure if this device is created
	 */
	bool Created() const
	{
		return m_bCreated;
	}

private:
	OpenCV_Camera_Device( const OpenCV_Camera_Device& );
	void operator=( const OpenCV_Camera_Device& );

	bool			m_bCreated;
	OniDeviceInfo*	m_pInfo;
	OniSensorInfo	m_sensors[1];
	oni::driver::DriverServices& m_driverServices;
};

/**
 * Driver
 */
class OpenCV_Camera_Driver : public oni::driver::DriverBase
{
public:
	/**
	 * Constructor
	 */
	OpenCV_Camera_Driver( OniDriverServices* pDriverServices ) : DriverBase( pDriverServices )
	{
		// default values
		m_bListDevice	= true;
		m_iMaxTestNum	= 10;
		m_sDeviceName	= "\\OpenCV\\Camera\\";
		m_sVendorName	= "OpenCV Camera by Heresy";

		// try to open setting files
		try
		{
			std::ifstream fileSetting( "OpenCVCamera.ini" );
			if( fileSetting.is_open() )
			{
				std::string sLine;
				while( std::getline( fileSetting, sLine ) )
				{
					// ignore comment begin with ';'
					if( sLine.size() < 5 || sLine[0] == ';' )
						continue;
	
					// split with '='
					size_t uPos = sLine.find_first_of( '=' );
					if( uPos != std::string::npos )
					{
						std::string sName	= sLine.substr( 0, uPos );
						std::string sValue	= sLine.substr( uPos+1 ); 
						if( sName == "device_name" )
						{
							m_sDeviceName = sValue;
						}
						else if( sName == "list_device" )
						{
							if( sValue == "0" )
								m_bListDevice = false;
						}
						else if( sName == "max_device_num" )
						{
							std::stringstream(sValue) >> m_iMaxTestNum;
						}
						else if( sName == "test_mode" )
						{
							OniVideoMode mMode;
							std::stringstream(sValue) >> mMode;
							m_vModeToTest.push_back( mMode );
						}
					}
				}
				fileSetting.close();
			}
			else
			{
				// default video mode
				m_vModeToTest.push_back( BuildMode( 320, 240, 30 ) );
				m_vModeToTest.push_back( BuildMode( 640, 480, 30 ) );
			}
		}
		catch( std::exception e )
		{
			getServices().errorLoggerAppend( "Setting file read error '%s'", e.what() );
		}
	}

	/**
	 * Initialize
	 */
	OniStatus initialize(	oni::driver::DeviceConnectedCallback connectedCallback,
							oni::driver::DeviceDisconnectedCallback disconnectedCallback,
							oni::driver::DeviceStateChangedCallback deviceStateChangedCallback,
							void* pCookie )
	{
		OniStatus eRes = oni::driver::DriverBase::initialize( connectedCallback, disconnectedCallback, deviceStateChangedCallback, pCookie );
		if( eRes == ONI_STATUS_OK )
		{
			if( m_bListDevice )
			{
				// test how many OpenCV devices work on this machine
				int iCounter = 0;
				while( iCounter < m_iMaxTestNum )
				{
					cv::VideoCapture mCamera( iCounter );
					if( mCamera.isOpened() )
					{
						mCamera.release();

						// construct URI
						std::stringstream ss;
						ss << m_sDeviceName << ( iCounter );

						// create device info
						CreateDeviceInfo( ss.str(), uint16_t(iCounter) );

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
		return eRes;
	}

	/**
	 * Open device
	 */
	oni::driver::DeviceBase* deviceOpen( const char* uri, const char* /*mode*/ )
	{
		std::string sUri = uri;

		// find if the device is already in the list
		auto itDevice = m_mDevices.find( sUri );
		if( itDevice != m_mDevices.end() )
		{
			auto& rDeviceData = itDevice->second;
			if( rDeviceData.second == NULL )
			{
				// create device if not created
				OpenCV_Camera_Device* pDevice = new OpenCV_Camera_Device( rDeviceData.first, m_vModeToTest, getServices() );
				if( pDevice->Created() )
				{
					rDeviceData.second = pDevice;
					return pDevice;
				}
				else
				{
					getServices().errorLoggerAppend( "Device '%s' create error", uri );
					delete pDevice;
					return NULL;
				}
			}
			else
			{
				// use created device directly
				return rDeviceData.second;
			}
		}

		getServices().errorLoggerAppend( "Can't find device: '%s'", uri );
		return NULL;
	}

	/**
	 * Close device
	 */
	void deviceClose( oni::driver::DeviceBase* pDevice )
	{
		// find in device list
		for( auto itDevice = m_mDevices.begin(); itDevice != m_mDevices.end(); ++ itDevice )
		{
			auto& rDeviceData = itDevice->second;
			if( rDeviceData.second == pDevice )
			{
				rDeviceData.second = NULL;
				delete pDevice;
				return;
			}
		}
	}

	/**
	 * Test given URI
	 */
	OniStatus tryDevice( const char* uri )
	{
		std::string sUri = uri;

		// Find in list first
		auto itDevice = m_mDevices.find( sUri );
		if( itDevice != m_mDevices.end() )
		{
			return ONI_STATUS_OK;
		}
		else
		{
			// not found existed, and does not listed when initialization
			if( !m_bListDevice )
			{
				// check if URI prefix is correct
				if( sUri.substr( 0, m_sDeviceName.length() ) == m_sDeviceName )
				{
					// get id
					try
					{
						std::string sIdx = sUri.substr( m_sDeviceName.length() );
						std::stringstream ss( sIdx );
						uint16_t uIdx;
						ss >> uIdx;
						CreateDeviceInfo( sUri, uIdx );
						return ONI_STATUS_OK;
					}
					catch( ... )
					{
						getServices().errorLoggerAppend( "given uri '%s' parsing error", uri );
						return ONI_STATUS_ERROR;
					}
				}
			}
		}

		return DriverBase::tryDevice(uri);
	}

	/**
	 * Shutdown
	 */
	void shutdown()
	{
		for( auto itDevice = m_mDevices.begin(); itDevice != m_mDevices.end(); ++ itDevice )
		{
			auto& rDeviceData = itDevice->second;
			delete rDeviceData.first;
			delete rDeviceData.second;
		}
	}

protected:
	/**
	 * prepare OniDeviceInfo and device list
	 */
	void CreateDeviceInfo( const std::string& sUri, uint16_t idx )
	{
		// Construct OniDeviceInfo
		OniDeviceInfo* pInfo = new OniDeviceInfo();
		strncpy( pInfo->vendor,	m_sVendorName.c_str(),	ONI_MAX_STR );
		strncpy( pInfo->name,	sUri.c_str(),			ONI_MAX_STR );
		strncpy( pInfo->uri,		sUri.c_str(),			ONI_MAX_STR );
		pInfo->usbProductId = uint16_t( idx );

		// save device info
		m_mDevices[sUri] = std::make_pair( pInfo, (oni::driver::DeviceBase*)NULL );
		deviceConnected( pInfo );
		deviceStateChanged( pInfo, 0 );
	}

protected:
	bool						m_bListDevice;
	int							m_iMaxTestNum;
	std::string					m_sDeviceName;
	std::string					m_sVendorName;
	std::vector<OniVideoMode>	m_vModeToTest;
	std::map< std::string,std::pair<OniDeviceInfo*, oni::driver::DeviceBase*> > m_mDevices;
};

ONI_EXPORT_DRIVER(OpenCV_Camera_Driver);
