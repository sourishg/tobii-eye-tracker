// SampleEyeXApp.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#define TX_NODEBUGOBJECT
#include <Windows.h>
#include <stdio.h>
#include <conio.h>
#include <assert.h>
#include <fstream>
#include <iostream>
#include <vector>
#include "eyex/EyeX.h"
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>

using namespace std;
using namespace cv;

Mat img;
Mat img2;
Mat scaleFactor;
Point cur_pt = Point(0, 0);
Point last_pt = Point(0, 0);
int scale_inc = 20;
ofstream out_file;
vector< Point > pts;

#pragma comment (lib, "Tobii.EyeX.Client.lib")

// ID of the global interactor that provides our data stream; must be unique within the application.
static const TX_STRING InteractorId = "Twilight Sparkle";

// global variables
static TX_HANDLE g_hGlobalInteractorSnapshot = TX_EMPTY_HANDLE;

/*
* Initializes g_hGlobalInteractorSnapshot with an interactor that has the Gaze Point behavior.
*/
BOOL InitializeGlobalInteractorSnapshot(TX_CONTEXTHANDLE hContext)
{
	TX_HANDLE hInteractor = TX_EMPTY_HANDLE;
	TX_GAZEPOINTDATAPARAMS params = { TX_GAZEPOINTDATAMODE_LIGHTLYFILTERED };
	BOOL success;

	success = txCreateGlobalInteractorSnapshot(
		hContext,
		InteractorId,
		&g_hGlobalInteractorSnapshot,
		&hInteractor) == TX_RESULT_OK;
	success &= txCreateGazePointDataBehavior(hInteractor, &params) == TX_RESULT_OK;

	txReleaseObject(&hInteractor);

	return success;
}

/*
* Callback function invoked when a snapshot has been committed.
*/
void TX_CALLCONVENTION OnSnapshotCommitted(TX_CONSTHANDLE hAsyncData, TX_USERPARAM param)
{
	// check the result code using an assertion.
	// this will catch validation errors and runtime errors in debug builds. in release builds it won't do anything.

	TX_RESULT result = TX_RESULT_UNKNOWN;
	txGetAsyncDataResultCode(hAsyncData, &result);
	assert(result == TX_RESULT_OK || result == TX_RESULT_CANCELLED);
}

/*
* Callback function invoked when the status of the connection to the EyeX Engine has changed.
*/
void TX_CALLCONVENTION OnEngineConnectionStateChanged(TX_CONNECTIONSTATE connectionState, TX_USERPARAM userParam)
{
	switch (connectionState) {
	case TX_CONNECTIONSTATE_CONNECTED: {
										   BOOL success;
										   printf("The connection state is now CONNECTED (We are connected to the EyeX Engine)\n");
										   // commit the snapshot with the global interactor as soon as the connection to the engine is established.
										   // (it cannot be done earlier because committing means "send to the engine".)
										   success = txCommitSnapshotAsync(g_hGlobalInteractorSnapshot, OnSnapshotCommitted, NULL) == TX_RESULT_OK;
										   if (!success) {
											   printf("Failed to initialize the data stream.\n");
										   }
										   else {
											   printf("Waiting for gaze data to start streaming...\n");
										   }
	}
		break;

	case TX_CONNECTIONSTATE_DISCONNECTED:
		printf("The connection state is now DISCONNECTED (We are disconnected from the EyeX Engine)\n");
		break;

	case TX_CONNECTIONSTATE_TRYINGTOCONNECT:
		printf("The connection state is now TRYINGTOCONNECT (We are trying to connect to the EyeX Engine)\n");
		break;

	case TX_CONNECTIONSTATE_SERVERVERSIONTOOLOW:
		printf("The connection state is now SERVER_VERSION_TOO_LOW: this application requires a more recent version of the EyeX Engine to run.\n");
		break;

	case TX_CONNECTIONSTATE_SERVERVERSIONTOOHIGH:
		printf("The connection state is now SERVER_VERSION_TOO_HIGH: this application requires an older version of the EyeX Engine to run.\n");
		break;
	}
}

float getVel(int x2, int y2, int x1, int y1) {
	float dx = (x2 - x1);
	float dy = (y2 - y1);
	return sqrt(dx*dx + dy*dy);
}
/*
* Handles an event from the Gaze Point data stream.
*/
void OnGazeDataEvent(TX_HANDLE hGazeDataBehavior)
{
	TX_GAZEPOINTDATAEVENTPARAMS eventParams;
	if (txGetGazePointDataEventParams(hGazeDataBehavior, &eventParams) == TX_RESULT_OK) {
		cur_pt.x = eventParams.X;
		cur_pt.y = eventParams.Y;
		pts.push_back(cur_pt);
		float vel = getVel(cur_pt.x, cur_pt.y, last_pt.x, last_pt.y);
		printf("Gaze Data: (%.1f, %.1f); Velocity: %.2f; timestamp %.0f ms\n", eventParams.X, eventParams.Y, vel, eventParams.Timestamp);
		out_file << cur_pt.x << "," << cur_pt.y << "," << vel << endl;
		last_pt.x = eventParams.X;
		last_pt.y = eventParams.Y;
	}
	else {
		printf("Failed to interpret gaze data event packet.\n");
	}
}

/*
* Callback function invoked when an event has been received from the EyeX Engine.
*/
void TX_CALLCONVENTION HandleEvent(TX_CONSTHANDLE hAsyncData, TX_USERPARAM userParam)
{
	TX_HANDLE hEvent = TX_EMPTY_HANDLE;
	TX_HANDLE hBehavior = TX_EMPTY_HANDLE;

	txGetAsyncDataContent(hAsyncData, &hEvent);

	// NOTE. Uncomment the following line of code to view the event object. The same function can be used with any interaction object.
	//OutputDebugStringA(txDebugObject(hEvent));

	if (txGetEventBehavior(hEvent, &hBehavior, TX_BEHAVIORTYPE_GAZEPOINTDATA) == TX_RESULT_OK) {
		OnGazeDataEvent(hBehavior);
		txReleaseObject(&hBehavior);
	}
	// NOTE since this is a very simple application with a single interactor and a single data stream, 
	// our event handling code can be very simple too. A more complex application would typically have to 
	// check for multiple behaviors and route events based on interactor IDs.

	txReleaseObject(&hEvent);
}

/*
* Application entry point.
*/
bool inImg(int x, int y) {
	if (x >= 0 && x < img.cols && y >= 0 && y < img.rows)
		return true;
	return false;
}

int main(int argc, char* argv[])
{
	out_file.open("data.csv");
	TX_CONTEXTHANDLE hContext = TX_EMPTY_HANDLE;
	TX_TICKET hConnectionStateChangedTicket = TX_INVALID_TICKET;
	TX_TICKET hEventHandlerTicket = TX_INVALID_TICKET;
	BOOL success;
	int mode = 2;
	const char* window = "IMG";
	namedWindow(window, CV_WINDOW_NORMAL);
	setWindowProperty(window, CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
	img = imread("gogh2.jpg", 1);
	img2 = Mat(img.rows, img.cols, CV_8UC3, Scalar(0, 0, 0));
	scaleFactor = Mat(img.rows, img.cols, CV_8UC1, Scalar(0));
	// initialize and enable the context that is our link to the EyeX Engine.
	success = txInitializeEyeX(TX_EYEXCOMPONENTOVERRIDEFLAG_NONE, NULL, NULL, NULL, NULL) == TX_RESULT_OK;
	success &= txCreateContext(&hContext, TX_FALSE) == TX_RESULT_OK;
	success &= InitializeGlobalInteractorSnapshot(hContext);
	success &= txRegisterConnectionStateChangedHandler(hContext, &hConnectionStateChangedTicket, OnEngineConnectionStateChanged, NULL) == TX_RESULT_OK;
	success &= txRegisterEventHandler(hContext, &hEventHandlerTicket, HandleEvent, NULL) == TX_RESULT_OK;
	success &= txEnableConnection(hContext) == TX_RESULT_OK;

	// let the events flow until a key is pressed.
	if (success) {
		printf("Initialization was successful.\n");
	}
	else {
		printf("Initialization failed.\n");
	}
	while (1)
	{
		if (waitKey(30) > 0) {
			break;
		}
		if (mode == 1) {
			Mat img1 = img.clone();
			circle(img1, cur_pt, 15, Scalar(0, 0, 255), -1, 8, 0);
			imshow(window, img1);
		}
		else {
			int w = 50;
			for (int i = -w; i <= w; i++) {
				for (int j = -w; j <= w; j++) {
					if (inImg(cur_pt.x + i, cur_pt.y + j)) {
						img2.at<Vec3b>(cur_pt.y + j, cur_pt.x + i) = img.at<Vec3b>(cur_pt.y + j, cur_pt.x + i);
						int scale = (int)scaleFactor.at<uchar>(cur_pt.y + j, cur_pt.x + i);
						if (scale + scale_inc < 255)
							scaleFactor.at<uchar>(cur_pt.y + j, cur_pt.x + i) = scale + scale_inc;
					}
				}
			}
			imshow(window, img2);
		}
	}
	Mat heatmap = Mat(img.rows, img.cols, CV_8UC3, Scalar(0, 0, 0));
	for (int i = 0; i < img.cols; i++) {
		for (int j = 0; j < img.rows; j++) {
			int scale = (int)scaleFactor.at<uchar>(j, i);
			int r = img.at<Vec3b>(j, i)[2];
			int g = img.at<Vec3b>(j, i)[1];
			int b = img.at<Vec3b>(j, i)[0];
			if (scale == 0) scale = scale_inc;
			if (scale < 85) {
				heatmap.at<Vec3b>(j, i)[2] = 0;
				heatmap.at<Vec3b>(j, i)[1] = g;
				heatmap.at<Vec3b>(j, i)[0] = 0;
			}
			else if (scale > 85 && scale < 170) {
				heatmap.at<Vec3b>(j, i)[2] = (r+g)/2;
				heatmap.at<Vec3b>(j, i)[1] = (r+g)/2;
				heatmap.at<Vec3b>(j, i)[0] = 0;
			}
			else {
				heatmap.at<Vec3b>(j, i)[2] = r;
				heatmap.at<Vec3b>(j, i)[1] = 0;
				heatmap.at<Vec3b>(j, i)[0] = 0;
			}
			/*
			heatmap.at<Vec3b>(j, i)[2] = ((float)scale / 255.) * (float)r;
			heatmap.at<Vec3b>(j, i)[1] = ((float)scale / 255.) * (float)g;
			heatmap.at<Vec3b>(j, i)[0] = ((float)scale / 255.) * (float)b;
			*/
		}
	}
	imwrite("scalefactor.jpg", scaleFactor);
	imwrite("heatmap.jpg", heatmap);
	for (int i = 1; i < pts.size(); i++) {
		line(img, pts[i], pts[i - 1], Scalar(0, 0, 255), 2, 8, 0);
	}
	imwrite("gaze_pattern.jpg", img);

	printf("Press any key to exit...\n");
	_getch();
	printf("Exiting.\n");
	out_file.close();
	// disable and delete the context.
	txDisableConnection(hContext);
	txReleaseObject(&g_hGlobalInteractorSnapshot);
	success = txShutdownContext(hContext, TX_CLEANUPTIMEOUT_DEFAULT, TX_FALSE) == TX_RESULT_OK;
	success &= txReleaseContext(&hContext) == TX_RESULT_OK;
	success &= txUninitializeEyeX() == TX_RESULT_OK;
	if (!success) {
		printf("EyeX could not be shut down cleanly. Did you remember to release all handles?\n");
	}

	return 0;
}

