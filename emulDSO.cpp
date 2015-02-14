#include "emulDSO.h"
#include <vector>
#include <windows.h>
#include <process.h>

#include <GdiPlus.h>
using namespace std;
using namespace Gdiplus;
#pragma comment(lib, "Gdiplus.lib")
#ifndef ULONG_PTR
#define ULONG_PRT unsigned long*
#endif

//gdiplus
static ULONG_PTR  gdiplusToken;

LRESULT CALLBACK WndProc( HWND, UINT, WPARAM, LPARAM ) ;        //声明用来处理消息的函数

static HANDLE dso_thread;

struct DSOCoordinate
{
	Rect rc;
	float x0, x1, y0, y1;
	float scale_x, scale_y;
};
struct DSOClass
{
	const char *	title;
	int				width;
	int				height;
	
	HANDLE	main_thread;
	HWND	hwnd;

	void create(void);
	void close(void);
	void display(HDC hdc);
	
	REAL			scale_time;
	
	vector<DSOCoordinate> coord;
	
	void push_coord(Graphics &graphics, Rect &rc, float x0, float x1,float y0, float y1);
	void pop_coord(Graphics &graphics);
	void set_coord(Graphics &graphics, DSOCoordinate &cc, bool bDrawAxis = false);
	void transform(Point polyPoints[], int cnt);
	void transform(PointF polyPoints[], int cnt);
	
	void draw_curve(Graphics &graphics);

	static unsigned __stdcall Main(void* param);
	static LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
};

static DSOClass dso;


void DSOClass::create(void)
{
	// Initialize GDI+.
	GdiplusStartupInput  gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	main_thread = (HANDLE)_beginthreadex(NULL, 0, Main, this, 0, NULL);

	scale_time = 1.0;
}
void DSOClass::close(void)
{
	DWORD thid = ::GetThreadId(main_thread);
	PostThreadMessage(thid, WM_QUIT,0,0);

	Gdiplus::GdiplusShutdown(gdiplusToken);
}

/*
	the transform in GDI+ will deform shapes, so is not suitable for our purpose
	so we have to do our own transform for x-scale and y-scale.
*/
void DSOClass::push_coord(Graphics &graphics, Rect &rc, 
						  float x0, float x1,
						  float y0, float y1)
{
	DSOCoordinate cc;
	cc.rc = rc;
	cc.y0 = y0;
	cc.y1 = y1;
	cc.x0 = x0;
	cc.x1 = x1;
	cc.scale_y = (float)rc.Height/(y1-y0);
	cc.scale_x = (float)rc.Width/(x1-x0);
	coord.push_back(cc);
	set_coord(graphics, coord.back(), true);
}
void DSOClass::pop_coord(Graphics &graphics)
{
	if(coord.size() > 0) coord.pop_back();
	graphics.ResetTransform();
	graphics.ResetClip();
	if(coord.size() > 0) set_coord(graphics, coord.back(), false);
}
void DSOClass::set_coord(Graphics &graphics, DSOCoordinate &cc, bool bDrawAxis)
{
	graphics.ResetTransform();
	graphics.ResetClip();

	// (y1 - y)/(y1-y0) = (py - rc.Y)/rc.Height
	// (y - y1)*scale_y = (py - rc.Y);
	// so when y = 0, py = rc.Y - y1*scale_y
	//
	graphics.TranslateTransform(cc.rc.X - cc.x0*cc.scale_x, cc.rc.Y + cc.y1*cc.scale_y);
	graphics.ScaleTransform(1, -1);

	//设置剪裁区域,这样数据曲线绘制时就无需考虑剪裁问题了
	Point polyPoints[] = {Point(cc.x0*cc.scale_x, cc.y0*cc.scale_y), 
						  Point(cc.x1*cc.scale_x, cc.y0*cc.scale_y), 
						  Point(cc.x1*cc.scale_x, cc.y1*cc.scale_y), 
						  Point(cc.x0*cc.scale_x, cc.y1*cc.scale_y)};
	GraphicsPath path;
	path.AddPolygon(polyPoints, 4);
	Region region(&path); // Construct a region based on the path.
	if(bDrawAxis)
	{
		// Draw the outline of the region.
		Pen pen(Color(255, 100, 100, 100));
		REAL dashValues[4] = {1, 0, 1, 0};
		pen.SetDashPattern(dashValues, 4);
		graphics.DrawPath(&pen, &path);
	}
	graphics.SetClip(&region);
}
void DSOClass::transform(Point polyPoints[], int cnt)
{
	DSOCoordinate &cc = coord.back();
	for(int i =0;i<cnt;i++)
	{
		polyPoints[i].X = polyPoints[i].X * cc.scale_x;
		polyPoints[i].Y = polyPoints[i].Y * cc.scale_y;
	}
}
void DSOClass::transform(PointF polyPoints[], int cnt)
{
	DSOCoordinate &cc = coord.back();
	for(int i =0;i<cnt;i++)
	{
		polyPoints[i].X = polyPoints[i].X * cc.scale_x;
		polyPoints[i].Y = polyPoints[i].Y * cc.scale_y;
	}
}


void DSOClass::draw_curve(Graphics &graphics)
{
	//绘图
	Pen greenPen(Color::Green, 3);
	PointF curvePoints[] = {PointF(0.0f, +10.0f),PointF(10.0f, -10.0f),PointF(20.0f, +10.0f),PointF(30.0f, -10.0f)}; 
	transform(curvePoints, 4);
	// Draw the curve.
	graphics.DrawCurve(&greenPen, curvePoints, 4);

	//Draw the points in the curve.
	SolidBrush redBrush(Color::Red);
	for(int i=0;i<4;i++)
		graphics.FillEllipse(&redBrush, Rect(curvePoints[i].X - 5 , curvePoints[i].Y - 5, 10, 10));
}

void DSOClass::display(HDC hdc)
{
	Graphics graphics(hdc);     //Graphics graphics(dc.m_hDC);也可以
	graphics.SetSmoothingMode(SmoothingModeHighQuality);
	
	//Brush
	push_coord(graphics, Rect(10, 10, 400, 200), 10, 20, -10,10);
	draw_curve(graphics);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK DSOClass::WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    HDC hdc ;
    PAINTSTRUCT ps;
    RECT rect;

	//此处直接使用dso全局变量
    switch( message )
    {
    case WM_CREATE:
        //MessageBox( hwnd, TEXT("窗口已创建完成!"), TEXT("我的窗口"), MB_OK | MB_ICONINFORMATION ) ;
        return 0;

    case WM_PAINT:
        hdc = BeginPaint( hwnd, &ps ) ;
        dso.display(hdc);
        EndPaint( hwnd, &ps ) ;
        return 0 ;

    case WM_LBUTTONDOWN:
        //MessageBox( hwnd, TEXT("鼠标左键被按下。"), TEXT("单击"), MB_OK | MB_ICONINFORMATION ) ;
        return 0;

    case WM_DESTROY:
        PostQuitMessage( 0 ) ;
        return 0;

	case WM_KEYDOWN:
		if(VK_ESCAPE == wParam);
        PostQuitMessage( 0 ) ;
        return 0;
    }
    return DefWindowProc( hwnd, message, wParam, lParam ) ;
}

unsigned __stdcall DSOClass::Main(void* param)
{
	static TCHAR szAppName[128] = TEXT("DSOWindow") ;
	DSOClass * pthis = (DSOClass *) param;
    MSG msg ;
    WNDCLASS wndclass ;
	HINSTANCE hInstance = ::GetModuleHandle(NULL);

    wndclass.style = CS_HREDRAW | CS_VREDRAW ;
    wndclass.lpszClassName = szAppName ;
    wndclass.lpszMenuName = NULL ;
    wndclass.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH) ;
    wndclass.lpfnWndProc = pthis->WndProc ;
    wndclass.cbWndExtra = 0 ;
    wndclass.cbClsExtra = 0 ;
    wndclass.hInstance = hInstance ;
    wndclass.hIcon = LoadIcon( NULL, IDI_APPLICATION ) ;
    wndclass.hCursor = LoadCursor( NULL, IDC_ARROW ) ;

	if( !RegisterClass( &wndclass ) ){
        MessageBox( NULL, TEXT("RegisterClass() failed!"), TEXT("error"), MB_OK | MB_ICONERROR ) ;
        return 0 ;
    }

    pthis->hwnd = CreateWindow(	szAppName,pthis->title,
								WS_OVERLAPPEDWINDOW,       //窗口的风格
								CW_USEDEFAULT,CW_USEDEFAULT,
								pthis->width, pthis->height,
								NULL,NULL,
								hInstance,
								NULL);

    ShowWindow( pthis->hwnd, SW_SHOW ) ;
    UpdateWindow( pthis->hwnd ) ;

    while( GetMessage( &msg, NULL, 0, 0 ) )
    {
        TranslateMessage( &msg ) ;
        DispatchMessage( &msg ) ;
    }
    return msg.wParam ;
}

//==========================================================================

void emulDSO_create(const char * title, int width, int height)
{
	dso.title = title;
	dso.width = width;
	dso.height = height;
	dso.create();
}

void emulDSO_close()
{
	dso.close();
}