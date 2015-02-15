#include "emulDSO.h"
#include <utility>
#include <vector>
#include <map>
#include <windows.h>
#include <process.h>

#include <GdiPlus.h>
using namespace std;
using namespace Gdiplus;
#pragma comment(lib, "Gdiplus.lib")
#ifndef ULONG_PTR
#define ULONG_PRT unsigned long*
#endif

#include "dataManager.hpp"

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

    int             vmargin;
    int             hmargin;

	HANDLE	        main_thread;
	HWND	        hwnd;
    
    Bitmap         * pmemBitmap;
    CachedBitmap   * pcachedBitmap;
    bool            bDirty;

    CRITICAL_SECTION critical_sec_data;
    ///////////////////////////////////////////////////////////////////////////////
    //data recording interface 
    DataManager      data_manager;
    void record(const char * data_name, const char * style, float value){
        ::EnterCriticalSection(&critical_sec_data);
        data_manager.record(data_name, style, value);
        ::LeaveCriticalSection(&critical_sec_data);
        bDirty = true;
        ::InvalidateRect(hwnd, NULL, TRUE);
    }
    void ticktock(float step_sec){ data_manager.ticktock(step_sec); }
    ///////////////////////////////////////////////////////////////////////////////

    void create(const char * ptitle, int plot_width, int plot_height);
	void close(void);
    void update(Graphics &graphics);
	void display(HDC hdc);
	
    float			scale_time;
    float           time_x0;
    float           time_x1;

	vector<DSOCoordinate> coord;
	
	void push_coord(Graphics &graphics, Rect &rc, float x0, float x1,float y0, float y1);
	void pop_coord(Graphics &graphics);
	void set_coord(Graphics &graphics, DSOCoordinate &cc, bool bDrawAxis = false);
	void transform(Point polyPoints[], int cnt);
	void transform(PointF polyPoints[], int cnt);
	
	void draw_curve(Graphics &graphics, data_info & di);

	static unsigned __stdcall Main(void* param);
	static LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
};

static DSOClass dso;


void DSOClass::create(const char * ptitle, int plot_width, int plot_height)
{
    InitializeCriticalSection(&critical_sec_data);

    title = ptitle;
    width = plot_width;
    height = plot_height;

    pmemBitmap = NULL;
    pcachedBitmap = NULL;
    bDirty = true;

    vmargin = 20;
    hmargin = 80;

 	// Initialize GDI+.
	GdiplusStartupInput  gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	main_thread = (HANDLE)_beginthreadex(NULL, 0, Main, this, 0, NULL);
	scale_time = 1.0;
    data_manager.clear();
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
	cc.scale_y = -(float)rc.Height/(y1-y0);
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

static int generate_ticks(float x0, float x1, int expect_tick_cnt, vector<float> &ticks)
{
    ticks.clear();
    //ticks should be as close as possible to the interval of 10^N
    float expect_resolution = (x1 - x0) / expect_tick_cnt;
    float fN = log10f(expect_resolution);
    int N = fN < 0 ? (int)(fN - 1) : fN;
    float step = pow(10, N);

    float tick_step;

    if (step > expect_resolution) tick_step = step;
    else if (2*step > expect_resolution) tick_step = 2*step;
    else if (5 * step > expect_resolution) tick_step = 5*step;
    else tick_step = 10 * step;

    float x = (int)(x0 / tick_step) * tick_step;
    for (; x < x1; x += tick_step)
    {
        if (x > x0) ticks.push_back(x);
    }
    return N;
}
void DSOClass::set_coord(Graphics &graphics, DSOCoordinate &cc, bool bDrawAxis)
{
	graphics.ResetTransform();
	graphics.ResetClip();

	// (y1 - y)/(y1-y0) = (py - rc.Y)/rc.Height
	// (y - y1)*scale_y = (py - rc.Y);
	// so when y = 0, py = rc.Y - y1*scale_y
	//
	graphics.TranslateTransform(cc.rc.X - cc.x0*cc.scale_x, cc.rc.Y - cc.y1*cc.scale_y);
	graphics.ScaleTransform(1, 1);

	//设置剪裁区域,这样数据曲线绘制时就无需考虑剪裁问题了
	Point polyPoints[] = {Point(cc.x0*cc.scale_x, cc.y0*cc.scale_y), Point(cc.x1*cc.scale_x, cc.y0*cc.scale_y), 
						  Point(cc.x1*cc.scale_x, cc.y1*cc.scale_y), Point(cc.x0*cc.scale_x, cc.y1*cc.scale_y)};
	GraphicsPath path;
	path.AddPolygon(polyPoints, 4);
	Region region(&path); // Construct a region based on the path.
	if(bDrawAxis)
	{
		// Draw the outline of the region.
		Pen pen(Color(255, 100, 100, 100), 1.0);
		REAL dashValues[4] = {1, 1, 1, 1};
		pen.SetDashPattern(dashValues, 4);
		graphics.DrawPath(&pen, &path);

        //Draw the ticks(with number): 
        Font font(L"Consolas", 16, FontStyleRegular, UnitPixel);
        StringFormat stringformat;
        stringformat.SetAlignment(StringAlignmentFar);
        stringformat.SetLineAlignment(StringAlignmentCenter);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        SolidBrush brush(Color(255, 100, 100, 100));
        RectF txtBox;
        graphics.MeasureString(L"99.90", 4, &font, RectF(), &stringformat, &txtBox);
        txtBox.X = cc.x0*cc.scale_x - hmargin;
        txtBox.Width = hmargin;

        //How many ticks can we display without overlapping
        vector<float> ticks;
        int N = generate_ticks(cc.y0, cc.y1, cc.rc.Height/txtBox.Height, ticks);

        WCHAR strinfo[32];
        for (int i = 0; i < ticks.size(); i++)
        {
            txtBox.Y = ticks[i] * cc.scale_y - txtBox.Height / 2;
            //if (ticks[i] - )
            if (N >= 0)  swprintf(strinfo, L"%.0f", ticks[i]);
            else if (N >= -1) swprintf(strinfo, L"%.1f", ticks[i]);
            else if (N >= -2) swprintf(strinfo, L"%.2f", ticks[i]);
            else if (N >= -3) swprintf(strinfo, L"%.3f", ticks[i]);
            else if (N >= -4) swprintf(strinfo, L"%.4f", ticks[i]);
            else swprintf(strinfo, L"%.05g", ticks[i]);
            graphics.DrawString(strinfo, wcslen(strinfo), &font, txtBox, &stringformat, &brush);
            graphics.DrawLine(&pen, PointF(cc.x0*cc.scale_x, ticks[i] * cc.scale_y), PointF(cc.x0*cc.scale_x + 5, ticks[i] * cc.scale_y));
        }
        
        stringformat.SetAlignment(StringAlignmentCenter);
        graphics.MeasureString(L" 99.90 ", 7, &font, RectF(), &stringformat, &txtBox);
        N = generate_ticks(cc.x0, cc.x1, (cc.rc.Width / txtBox.Width), ticks);
        txtBox.Y = cc.y0*cc.scale_y;
        for (int i = 0; i < ticks.size(); i++)
        {
            txtBox.X = ticks[i] * cc.scale_x - txtBox.Width / 2;
            if (N >= 0)  swprintf(strinfo, L"%.0f", ticks[i]);
            else if (N >= -1) swprintf(strinfo, L"%.1f", ticks[i]);
            else if (N >= -2) swprintf(strinfo, L"%.2f", ticks[i]);
            else swprintf(strinfo, L"%.05g", ticks[i]);
            graphics.DrawString(strinfo, wcslen(strinfo), &font, txtBox, &stringformat, &brush);
            graphics.DrawLine(&pen, PointF(ticks[i] * cc.scale_x, cc.y0*cc.scale_y), PointF(ticks[i] * cc.scale_x, cc.y0*cc.scale_y - 5));
        }
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


void DSOClass::draw_curve(Graphics &graphics, data_info & di)
{
	//绘图
	
	//PointF curvePoints[] = {PointF(0.0f, +10.0f),PointF(10.0f, -10.0f),PointF(20.0f, +10.0f),PointF(30.0f, -10.0f)}; 
    //transform(curvePoints, 4);

    
    DSOCoordinate &cc = coord.back();

    //style: analog, digital

    if (strstr(di.style, "d"))
    {
    }
    else
    {
        const char * pcfg;
        Pen pen(Color::Green, 1);
        const Color color_table[] = { Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255),
            Color(0x80, 0, 0), Color(0x80, 0, 0x80), Color(0x48, 0xd1, 0xcc),
            Color(0xFF, 0xC0, 0xCB), Color(0x80, 0x80, 0), Color(0, 0, 0x80) };
        REAL dashValues[4] = { 1, 1, 1, 1 };
        

        pcfg = strstr(di.style, "c");
        if (pcfg) pen.SetColor(color_table[pcfg[1] - '0']);
        pcfg = strstr(di.style, "w");
        if (pcfg) pen.SetWidth(pcfg[1] - '0');
        pcfg = strstr(di.style, ".");
        if (pcfg) pen.SetDashPattern(dashValues, 2);
        

        PointF * curvePoints = new PointF[di.data.size()];
        for (int i = 0; i < di.data.size(); i++)
        {
            curvePoints[i].X = di.data[i].first * cc.scale_x;
            curvePoints[i].Y = di.data[i].second * cc.scale_y;
        }
        // Draw the curve.
        graphics.DrawCurve(&pen, curvePoints, di.data.size());

        //draw point
        if (strstr(di.style, "p"))
        {
            //Draw the points in the curve.
            Color color(0x80,0,0);
            pen.GetColor(&color);
            SolidBrush redBrush(color);
            int pw = pen.GetWidth();
            for (int i = 0; i < di.data.size(); i++)
                graphics.FillRectangle(&redBrush, Rect(curvePoints[i].X - pw, curvePoints[i].Y - pw, pw * 2 + 1, pw * 2 + 1));
        }
        delete[]curvePoints;
    }

}

void DSOClass::update(Graphics &graphics)
{

    //graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush BgBrush(Color(255,255,255,255));
    graphics.FillRectangle(&BgBrush, 0, 0, width, height);

    //draw signal one by one, but we needs to figure out the groups and data ranges
    //before the painting, better way is keep these information maintained rather than
    //calculate them before each painting:
    //   *group list is only created when new data name are met
    //   *data range may keep changing when new data arrived
    // these are all maintained by data_manager now;

    //coordinates information is fixed
    time_x0 = 0; time_x1 = data_manager.record_time;
    //time_x0 = 1.0; time_x1 = 2.5;// data_manager.record_time;

    EnterCriticalSection(&critical_sec_data);

    ::RECT client_rc;
    ::GetClientRect(this->hwnd, &client_rc);

    int plot_cnt = data_manager.gname2ids.size();
    int x0 = client_rc.left + hmargin;
    int width = client_rc.right - client_rc.left - 2 * hmargin;
    int height = ((client_rc.bottom - client_rc.top - 20) / plot_cnt) - vmargin * 2;
    int y = 0;
    for (std::map<std::string, std::vector<int>>::reverse_iterator igname2id = data_manager.gname2ids.rbegin();
        igname2id != data_manager.gname2ids.rend();
        igname2id++)
    {
        std::vector<int> &ids = igname2id->second;

        //get overall data range for group
        float range_min = FLT_MAX;
        float range_max = -FLT_MAX;
        for (int j = 0; j < ids.size(); j++)
        {
            data_info &di = data_manager.data[ids[j]];
            if (range_min > di.range_min) range_min = di.range_min;
            if (range_max < di.range_max) range_max = di.range_max;
        }
        float range = range_max - range_min;
        float margin = range * 0.03f;

        //setup coordinate and draw
        push_coord(graphics, Rect(x0, y + vmargin, width, height), time_x0, time_x1, range_min - margin, range_max + margin);
        for (int j = 0; j < ids.size(); j++)
        {
            data_info &di = data_manager.data[ids[j]];
            draw_curve(graphics, di);
        }
        pop_coord(graphics);

        y += height + 2 * vmargin;
    }

    LeaveCriticalSection(&critical_sec_data);
}

void DSOClass::display(HDC hdc)
{
	Graphics graphics(hdc);     //Graphics graphics(dc.m_hDC);也可以

    if (bDirty)
    {
        if (pmemBitmap) delete pmemBitmap;
        if (pcachedBitmap) delete pcachedBitmap;
        RECT rc;
        ::GetClientRect(hwnd, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
        pmemBitmap = new Bitmap(width, height, &graphics);
        update(*Graphics::FromImage(pmemBitmap));
        pcachedBitmap = new CachedBitmap(pmemBitmap, &graphics);
        bDirty = false;
    }

    if (!bDirty)
    {
        if (graphics.DrawCachedBitmap(pcachedBitmap, 0, 0) != Ok) 
            bDirty = true;
    }
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
    case WM_SIZE:
        dso.bDirty = true;
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
    wndclass.hbrBackground = NULL;// (HBRUSH)GetStockObject(WHITE_BRUSH);
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

    pthis->hwnd = CreateWindow(szAppName, szAppName,//pthis->title,
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
    dso.create(title, width, height);
}
void emulDSO_close(int waitForUser)
{
    if (waitForUser)
    {
        //let user control when to exit
        ::WaitForSingleObject(dso.main_thread, INFINITE);
    }
    else
        dso.close();
}
void emulDSO_record(const char * data_name, const char * style, float value)
{
    dso.record(data_name, style, value);
}
void emulDSO_ticktock(float step_sec)
{
    dso.ticktock(step_sec);
}
