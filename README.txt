WebCam4OpenNI2
==============

The driver of generic webcam for OpenNI 2 to get color VideoStream via OpenCV.

To use this driver module, simply put the precompiled dll file(OpenCVCamera32.dll or OpenCVCamera64.dll) in the folder "\OpenNI2\Drivers". 
(Where should also has PS1080.dll, OniFile.dll and Kinect.dll)

Binary link: https://github.com/KHeresy/WebCam4OpenNI2/tree/master/Binary

Then, you can create the Device and color VideoStream of webcam.

Notice:

1. Most of the middleware libraries are work based on depth map, so this driver module can't make a webcam replace a depth camera.

2. The driver module may has higher priority than OpenNI, so openni::ANY_DEVICE may get a webcam, not Xtion.

3. You may need to install "Visual C++ Redistributable for Visual Studio 2012 Update 1", since I build the binary with Visual Studio 2012.
   The files could be found at: http://www.microsoft.com/en-us/download/details.aspx?id=30679


==============

User can modify some setting of this driver module by put the file "OpenCVCamera.ini" in the folder where the dll file exists.

The sample of this file: https://github.com/KHeresy/WebCam4OpenNI2/blob/master/OpenCVCamera.ini

1. "device_name" is the prefix of uri. The full URI is the prefix and a number.
For example, the uri of first webcam is "\OpenCV\Camera\0".

2. Set "list_device" as 0 if you don't want to use this device immediately.
The webcam will not been list when call OpenNI::enumerateDevices(), but you still can create the device by specified uri.

3. "max_device_num" is the maximum number of webcam to test.
Need to set this is because some issue of OpenCV.

4. "test_mode" is the resolution that this device may use.
Because OpenCV can't get the support list, so we need to list the modes we want to use.


==============

To build this by youeself, you need OpenNI2 source code and OpenCV

1. https://github.com/OpenNI/OpenNI2

2. http://opencv.org/

Please put the files "OpenCVCamera.cpp" and "OpenCVCamera.vcxproj" in a folder under OpenNI2\Source\Drivers\ (ex: OpenNI2\Source\Drivers\WebCam4OpenNI2), and modify the project setting for OpenCV path.

Because I use C++11 thread in this project, which is not supported in VC2010, so you may need to use boost::thread to replace it when using Visual Studio 2010.

Although I only test under Windows, but this module should also work under other platforms.