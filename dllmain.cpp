// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <iostream>
#include <fstream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include "SHA512.h"

#include <Iphlpapi.h>
#include <Assert.h>
#include <algorithm>
#include <cctype>
#include <intrin.h>

#define EAX 0
#define EBX 1
#define ECX 2
#define EDX 3

#define	BUTTON_SPLIT	0
#define	BUTTON_DOUBLE	1
#define	BUTTON_HIT		2
#define	BUTTON_STAND	3
#define	BUTTON_NEW		4
#define	BUTTON_REBET	5
#define	BUTTON_REDIL	6
#define	BUTTON_SILENT	7

using namespace std;

/**
 * Function to perform fast template matching with image pyramid
 */

double other_threshold = 0.9935;

CRITICAL_SECTION g_global_lock;
bool g_initialized = false;
HANDLE g_module;

cv::Rect loginRect{ 1490,0,430,140 };

struct CriticalSection {
	CRITICAL_SECTION* plock;
	CriticalSection(CRITICAL_SECTION& lock) {
		plock = &lock;
		EnterCriticalSection(plock);
	}
	~CriticalSection() {
		LeaveCriticalSection(plock);
	}
};


BITMAPINFOHEADER createBitmapHeader(int width, int height)
{
	BITMAPINFOHEADER  bi;

	// create a bitmap
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height;  //this is the line that makes it draw upside down or not
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = 0;
	bi.biXPelsPerMeter = 0;
	bi.biYPelsPerMeter = 0;
	bi.biClrUsed = 0;
	bi.biClrImportant = 0;

	return bi;
}

cv::Mat captureScreenMat(HWND hwnd)
{
	cv::Mat src;

	// get handles to a device context (DC)
	HDC hwindowDC = GetDC(hwnd);
	HDC hwindowCompatibleDC = CreateCompatibleDC(hwindowDC);
	SetStretchBltMode(hwindowCompatibleDC, COLORONCOLOR);

	// define scale, height and width
	int screenx = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int screeny = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	// create mat object
	src.create(height, width, CV_8UC4);

	// create a bitmap
	HBITMAP hbwindow = CreateCompatibleBitmap(hwindowDC, width, height);
	BITMAPINFOHEADER bi = createBitmapHeader(width, height);

	// use the previously created device context with the bitmap
	SelectObject(hwindowCompatibleDC, hbwindow);

	// copy from the window device context to the bitmap device context
	StretchBlt(hwindowCompatibleDC, 0, 0, width, height, hwindowDC, screenx, screeny, width, height, SRCCOPY);  //change SRCCOPY to NOTSRCCOPY for wacky colors !
	GetDIBits(hwindowCompatibleDC, hbwindow, 0, height, src.data, (BITMAPINFO*)& bi, DIB_RGB_COLORS);            //copy from hwindowCompatibleDC to hbwindow

	// avoid memory leak
	DeleteObject(hbwindow);
	DeleteDC(hwindowCompatibleDC);
	ReleaseDC(hwnd, hwindowDC);

	return src;
}

/**
	if not found anything, return 0
	if found blackjack, return 1
	if found from 4 to 21, return 4~21
	if found from pattern "A,n" , return 30 + n (32~40)
*/

int myCardSearch(cv::Mat ref, int type = 0)
{
	cv::Rect r(850, 440, 180, 50);
	cv::Rect slashR(915, 690, 65, 45);
	if (type != 0)
	{
		r = cv::Rect(850, 690, 180, 50);
	}
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	cv::Mat slash_region, slash_gray;

	//capture = cv::imread("test\\9.png");
	//	search_region = ref;
	ref.copyTo(capture);
	ref(r).copyTo(search_region);
	ref(slashR).copyTo(slash_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	cv::cvtColor(slash_region, slash_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn, mask_fn;
	double minVal = 0, maxVal = 0;

	// search blackjack

	fn = "temp\\my_mark_black.png";
	mask_fn = "temp\\my_mark_black_mask.png";
	templ = cv::imread(fn);
	mask = cv::imread(mask_fn);
	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
	cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		matchLoc = maxLoc + r.tl();
		//matchLoc.x = maxLoc.x + r.x;
		//matchLoc.y = maxLoc.y + r.y;
		cout << "Card Value: BlackJack" << endl;
		cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;

		cv::rectangle(
			capture,
			matchLoc,
			cv::Point(matchLoc.x + templ.cols, matchLoc.y + templ.rows),
			CV_RGB(255, 0, 0),
			1);
		//cv::imshow("search_ref", capture);
		return 1;
	}

	// search A,n

	for (int i = 10; i >= 2; i--)
	{
		buf.clear();
		buf << "temp\\my_mark_" << (i + 10) << "_" << i << ".png";
		buf >> fn;

		buf.clear();
		buf << "temp\\my_mark_" << (i + 10) << "_" << i << "_mask.png";
		buf >> mask_fn;

		templ = cv::imread(fn);
		mask = cv::imread(mask_fn);
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
		cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		minVal = 0; maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			matchLoc = maxLoc + r.tl();
			//matchLoc.x = maxLoc.x + r.x;
			//matchLoc.y = maxLoc.y + r.y;
			cout << "Card Value:" << (i + 10) << "_" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return 30 + i;
		}
	}

	// Search slash

	fn = "temp\\slash.png";
	mask_fn = "temp\\slash_mask.png";
	templ = cv::imread(fn);
	mask = cv::imread(mask_fn);
	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
	cv::matchTemplate(slash_gray, tpl_gray, dst, match_method, mask_gray);
	minVal = 0; maxVal = 0;
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > 0.99)
	{
		cout << "Slash found..." << endl;
		return 0;
	}

	// Search n

	for (int i = 21; i >= 4; i--)
	{
		buf.clear();
		buf << "temp\\my_mark_" << i << ".png";
		buf >> fn;

		buf.clear();
		buf << "temp\\my_mark_" << i << "_mask.png";
		buf >> mask_fn;

		templ = cv::imread(fn);
		mask = cv::imread(mask_fn);
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
		cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		minVal = 0; maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			matchLoc = maxLoc + r.tl();
			//matchLoc.x = maxLoc.x + r.x;
			//matchLoc.y = maxLoc.y + r.y;
			cout << "Card Value:" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return i;
		}

		if (i == 18)
		{
			buf.clear();
			buf << "temp\\my_mark_" << i << "(1).png";
			buf >> fn;

			buf.clear();
			buf << "temp\\my_mark_" << i << "(1)_mask.png";
			buf >> mask_fn;

			templ = cv::imread(fn);
			mask = cv::imread(mask_fn);
			cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
			cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
			cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
			minVal = 0; maxVal = 0;
			cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
			if (maxVal > other_threshold)
			{
				matchLoc = maxLoc + r.tl();
				//matchLoc.x = maxLoc.x + r.x;
				//matchLoc.y = maxLoc.y + r.y;
				cout << "Card Value:" << i << endl;
				cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
				//cv::imshow("search_ref", ref);
				return i;
			}
		}
	}
	//cout << "No found!!!" << endl;
	return 0;
}

/*
	if not found anyone, return 0
	if found someone, return 1~10
*/

int otherCardSearch(cv::Mat ref)
{

	cout << "Search another card ..." << endl;

	cv::Rect r(870, 290, 100, 50);
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	//capture = cv::imread("test\\9.png");
//	search_region = ref;
	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn, mask_fn;
	// Search n

	mask = cv::imread("temp\\another_black_mask.png");
	templ = cv::imread("temp\\another_black.png");

	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);

	cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		matchLoc.x = maxLoc.x + r.x;
		matchLoc.y = maxLoc.y + r.y;
		cout << "Card Value: black jack" << endl;
		cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
		return 21;
	}

	mask = cv::imread("temp\\another_mark_20_mask.png");
	templ = cv::imread("temp\\another_mark_20.png");

	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);

	cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		matchLoc.x = maxLoc.x + r.x;
		matchLoc.y = maxLoc.y + r.y;
		cout << "Card Value: 20" << endl;
		cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
		return 20;
	}


	for (int i = 19; i >= 12; i--)
	{
		buf.clear();
		buf << "temp\\small_" << i << ".png";
		buf >> fn;

		buf.clear();
		buf << "temp\\small_" << i << "_mask.png";
		buf >> mask_fn;

		templ = cv::imread(fn);
		mask = cv::imread(mask_fn);
		if (templ.empty()) continue;
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
		cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		minVal = 0; maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			matchLoc = maxLoc + r.tl();
			//matchLoc.x = maxLoc.x + r.x;
			//matchLoc.y = maxLoc.y + r.y;
			cout << "Small card value:" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return 0;
		}
	}

	mask = cv::imread("temp\\another_mark_10_mask.png");
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);

	for (int i = 10; i >= 1; i--)
	{
		buf.clear();
		buf << "temp\\another_mark_" << i << ".png";
		buf >> fn;
		templ = cv::imread(fn);
		cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		if (i == 10)
		{
			cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		}
		else
		{
			cv::matchTemplate(search_gray, tpl_gray, dst, match_method);
		}
		minVal = 0, maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			//matchLoc = maxLoc + r.tl();
			matchLoc.x = maxLoc.x + r.x;
			matchLoc.y = maxLoc.y + r.y;
			cout << "Card Value:" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return i;
		}
	}
	cout << "No found!!!" << endl;
	return 0;
}

/*
	if found someone, return true.
	if not found anyone, return false.
*/


bool loginButtonSearch(cv::Mat ref)
{
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(loginRect).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;
	bool return_val = false;

	fn = "temp\\login_button.png";

	templ = cv::imread(fn);
	cv::matchTemplate(search_region, templ, dst, match_method);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		return true;
	}

	return false;
}

bool logoutButtonSearch(cv::Mat ref)
{
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(loginRect).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;
	bool return_val = false;

	fn = "temp\\logout_button.png";

	templ = cv::imread(fn);
	cv::matchTemplate(search_region, templ, dst, match_method);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		return true;
	}

	return false;
}


/*
	if found someone, return true.
	if not found anyone, return false.
*/

bool buttonSearch(cv::Mat ref, int type)
{
	cv::Rect r(550, 865, 815, 145);
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;
	bool return_val = false;

	switch (type)
	{
	case BUTTON_SPLIT: // Search split button
		fn = "temp\\btn_split.png";
		break;
	case BUTTON_DOUBLE: // Search bouble button
		fn = "temp\\btn_double.png";
		break;
	case BUTTON_HIT: // Search hit button
		fn = "temp\\btn_hit.png";
		break;
	case BUTTON_STAND:// Search stand button
		fn = "temp\\btn_stand.png";
		break;
	case BUTTON_NEW: // Search 'new_game' button
		fn = "temp\\btn_new_game.png";
		break;
	case BUTTON_REBET: // Search rebet button
		fn = "temp\\btn_rebet.png";
		break;
	case BUTTON_REDIL: // Search 'rebet&dil' button
		fn = "temp\\btn_rebet&dil.png";
		break;
	case BUTTON_SILENT: // Search 'silent' button
		fn = "temp\\btn_silent.png";
		break;
	default:
		return false;
		break;
	}

	templ = cv::imread(fn);
	cv::matchTemplate(search_region, templ, dst, match_method);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		return true;
	}

	return false;

}

/*
	if found alert, return true.
	if not found alert, return false.
*/

bool alertSearch(cv::Mat ref)
{
	cv::Rect r(970, 510, 130, 60);
	cv::Mat capture, search_region, templ;
	cv::Mat search_gray, tpl_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;

	// Search alert
	fn = "temp\\insurance.png";
	templ = cv::imread(fn);
	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::matchTemplate(search_gray, tpl_gray, dst, match_method);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		cout << "Alert found" << endl;
		return true;
	}
	cout << "Alert not found" << endl;
	return false;
}


// After insurance is shown, search hit button

bool newBtnSearch(cv::Mat ref)
{
	cout << "Search new button ..." << endl;

	cv::Rect r(650, 880, 620, 120);
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;
	bool return_val = false;

	templ = cv::imread("temp\\btn_ins_new.png");

	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::matchTemplate(search_gray, tpl_gray, dst, match_method);

	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		return true;
	}

	return false;

}

// Search Error

bool errorSearch(cv::Mat ref)
{
	cout << "Search error message ..." << endl;

	cv::Rect r(750, 300, 400, 120);
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;
	double minVal = 0, maxVal = 0;

	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn;
	bool return_val = false;

	templ = cv::imread("temp\\error.png");
	//mask = cv::imread("temp\\error_mask.png");

	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	//cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
	cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);

	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	cout << maxVal << endl;
	if (maxVal > other_threshold)
	{
		cout << "Error message showed..." << endl;
		return true;
	}

	return false;

}


int mySmallSearch(cv::Mat ref)
{
	cout << "Search my small card ..." << endl;

	cv::Rect r(850, 690, 180, 50);
	cv::Mat capture, search_region, templ, mask;
	cv::Mat search_gray, tpl_gray, mask_gray;
	cv::Mat dst;
	int match_method = CV_TM_CCORR_NORMED;
	cv::Point minLoc, maxLoc, matchLoc;

	//capture = cv::imread("test\\9.png");
	//	search_region = ref;
	ref.copyTo(capture);
	ref(r).copyTo(search_region);
	cv::cvtColor(search_region, search_gray, CV_BGR2GRAY);
	stringstream buf;
	string fn, mask_fn;
	double minVal = 0, maxVal = 0;

	// search blackjack

	fn = "temp\\my_mark_black.png";
	mask_fn = "temp\\my_mark_black_mask.png";
	templ = cv::imread(fn);
	mask = cv::imread(mask_fn);
	cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
	cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
	cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
	cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
	if (maxVal > other_threshold)
	{
		matchLoc = maxLoc + r.tl();
		//matchLoc.x = maxLoc.x + r.x;
		//matchLoc.y = maxLoc.y + r.y;
		cout << "Small card value: BlackJack" << endl;
		cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;

		cv::rectangle(
			capture,
			matchLoc,
			cv::Point(matchLoc.x + templ.cols, matchLoc.y + templ.rows),
			CV_RGB(255, 0, 0),
			1);
		//cv::imshow("search_ref", capture);
		return 1;
	}

	// search A,n

	for (int i = 10; i >= 2; i--)
	{
		buf.clear();
		buf << "temp\\small_" << (i + 10) << "_" << i << ".png";
		buf >> fn;

		buf.clear();
		buf << "temp\\small_" << (i + 10) << "_" << i << "_mask.png";
		buf >> mask_fn;

		templ = cv::imread(fn);
		mask = cv::imread(mask_fn);
		if (templ.empty()) continue;
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
		cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		minVal = 0; maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			matchLoc = maxLoc + r.tl();
			//matchLoc.x = maxLoc.x + r.x;
			//matchLoc.y = maxLoc.y + r.y;
			cout << "Small card value:" << (i + 10) << "_" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return 30 + i;
		}
	}

	// Search n

	for (int i = 21; i >= 4; i--)
	{
		buf.clear();
		buf << "temp\\small_" << i << ".png";
		buf >> fn;

		buf.clear();
		buf << "temp\\small_" << i << "_mask.png";
		buf >> mask_fn;

		templ = cv::imread(fn);
		mask = cv::imread(mask_fn);
		if (templ.empty()) continue;
		cv::cvtColor(templ, tpl_gray, CV_BGR2GRAY);
		cv::cvtColor(mask, mask_gray, CV_BGR2GRAY);
		cv::matchTemplate(search_gray, tpl_gray, dst, match_method, mask_gray);
		minVal = 0; maxVal = 0;
		cv::minMaxLoc(dst, NULL, &maxVal, NULL, &maxLoc, cv::Mat());
		if (maxVal > other_threshold)
		{
			matchLoc = maxLoc + r.tl();
			//matchLoc.x = maxLoc.x + r.x;
			//matchLoc.y = maxLoc.y + r.y;
			cout << "Small card value:" << i << endl;
			cout << "x: " << matchLoc.x << " y:" << matchLoc.y << endl;
			//cv::imshow("search_ref", ref);
			return i;
		}
	}
	//cout << "No found!!!" << endl;
	return 0;
}

string getKey1(string strOrg)
{
	SHA512 sha512;
	string strKey1;

	std::string strTmp = strOrg;
	std::string result(sha512.hash(strTmp));

	strKey1 = result.substr(0, 4).c_str();
	return strKey1;
}

string getKey2(string strOrg)
{
	SHA512 sha512;
	string strResult;

	strOrg += "getKey2";

	std::string strTmp = strOrg;
	std::string result(sha512.hash(strTmp));

	strResult = result.substr(0, 4).c_str();
	return strResult;
}

string getKey3(string strOrg)
{
	SHA512 sha512;
	string strResult;

	strOrg += "getKey3";

	std::string strTmp = strOrg;
	std::string result(sha512.hash(strTmp));

	strResult = result.substr(0, 4).c_str();
	return strResult;
}

string getKey4(string strOrg)
{
	SHA512 sha512;
	string strResult;

	strOrg += "getKey4";

	std::string strTmp = strOrg;
	std::string result(sha512.hash(strTmp));

	strResult = result.substr(0, 4).c_str();
	return strResult;
}

string getUpperString(string strOrg)
{
	std::string data = strOrg;

	std::for_each(data.begin(), data.end(), [](char& c) {
		c = toupper(c);
	});

	return data;
}

string generateKey()
{
	int exx[4];
	char buffer[100];
	string str_cpuid = "";
	SHA512 sha512;
	string strKey;

	for (int i = 0; i < 5; i++)
	{
		__cpuid(exx, i);

		sprintf_s(buffer, "%08X", exx[EAX]);
		str_cpuid += buffer;
	}


	string result(sha512.hash(str_cpuid));
	string key1 = getKey1(result);
	string key2 = getKey1(key1);
	string key3 = getKey1(key2);
	string key4 = getKey1(key3);

	strKey = getUpperString(key1) + "-"
		+ getUpperString(key2) + "-"
		+ getUpperString(key3) + "-"
		+ getUpperString(key4) +"-K";

	return strKey;
}

string generateSN(string strKey)
{
	SHA512 sha512;
	string hashResult;

	std::string data = strKey;
	std::string result(sha512.hash(data));

	string strSn1 = getKey1(result);
	string strSn2 = getKey1(strSn1);
	string strSn3 = getKey1(strSn2);
	string strSn4 = getKey1(strSn3);

	string strSn = getUpperString(strSn1) + "-"
		+ getUpperString(strSn2) + "-"
		+ getUpperString(strSn3) + "-"
		+ getUpperString(strSn4) + "-S";

	return strSn;
}


/*********************************************************/
/**************** External function list *****************/
/*********************************************************/

extern "C" __declspec(dllexport) int isLicensed()
{
	string fn = "license.key";
	ifstream f(fn.c_str());
	FILE* fp;
	string key = generateKey();
	string sn = generateSN(key);
	string str;
	char scan_data[256];
	int strLen = 0;
	if (f.good())
	{
		fopen_s(&fp, fn.c_str(), "r");
		fscanf_s(fp, "%s", scan_data);
		fclose(fp);
		str = string(scan_data);
		strLen = str.length();
		if (str.at(strLen - 1) == 'S') // if data is license key...
		{
			string sn1 = str.substr(0, strLen);
			if (sn.compare(sn1) == 0)
			{
				return 1;
			}
			return 0;
		}
		else
		{
			fopen_s(&fp, fn.c_str(), "w");
			fprintf(fp, "%s", key.c_str());
			fclose(fp);
			return 0;
		}
	}
	else
	{
		fopen_s(&fp, fn.c_str(), "w");
		fprintf(fp, "%s", key.c_str());
		fclose(fp);
		return 0;
	}
	//fopen_s(&fp, fn.c_str(), "w");
	//fprintf_s(fp, "%s\n%s", key.c_str(), sn.c_str());
	//fclose(fp);
	return 0;
}

extern "C" __declspec(dllexport) int find_anothercard()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return otherCardSearch(src);
}

extern "C" __declspec(dllexport) int find_mycard()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	int ret = myCardSearch(src);
	if (ret == 0) 
	{
		return myCardSearch(src, 1);
	}
	else if (ret == 21)
	{
		Sleep(3000);
		return myCardSearch(src, 1);
	}
	else
	{
		return ret;
	}
}

extern "C" __declspec(dllexport) int find_smallcard()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return mySmallSearch(src);

}

extern "C" __declspec(dllexport) int find_button(int type)
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return buttonSearch(src, type) ? 1 : 0;
}

extern "C" __declspec(dllexport) int find_alert()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return alertSearch(src) ? 1 : 0;
}

extern "C" __declspec(dllexport) int find_ins_hit()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return newBtnSearch(src) ? 0 : 1;
}

extern "C" __declspec(dllexport) int find_login_button()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return loginButtonSearch(src) ? 0 : 1;
}

extern "C" __declspec(dllexport) int find_logout_button()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return logoutButtonSearch(src) ? 0 : 1;
}

extern "C" __declspec(dllexport) int find_error_msg()
{
	HWND hwnd = GetDesktopWindow();
	cv::Mat src = captureScreenMat(hwnd);
	cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);

	return errorSearch(src) ? 0 : 1;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
		g_module = hModule;
		InitializeCriticalSection(&g_global_lock);
	case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
		DeleteCriticalSection(&g_global_lock);
		break;
    }
    return TRUE;
}

