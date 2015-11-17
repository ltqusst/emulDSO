#include <tchar.h>
#define USE_EMUL_DSO
#include "emulDSO.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>
#include <utility>
#include <string>
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



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#include "dataManager.hpp"

struct data_entry
{
    union {
        float time;
        float x;
    };
    union {
        float value;
        float y;
    };
};

struct data_info
{
    int id;     //data id
    int gid;    //group id
    TCHAR name[64];
    TCHAR style[256];
    std::vector<data_entry> data;
    float range_min;
    float range_max;

	void data_id_range(float x0, float x1, int &id0, int &id1)
	{
		for(id0=0;id0<data.size();id0++) if(data[id0].time >= x0) break;
		if(id0 > 0) id0 --;

		for(id1=data.size()-1;id1>=id0;id1--) if(data[id1].time <= x1) break;
		if(id1 < data.size()-1) id1 ++;
	}
};
struct group_info
{
    bool bIsDigital;
    float range_min;
    float range_max;
    std::vector<int> ids;   //data id of this group
};
static void get_group_name(const TCHAR * name, TCHAR * gname)
{
    const TCHAR * pdot = _tcsrchr(name, _TEXT('.'));
    int cnt = _tcslen(name);
    if (pdot)
        cnt = pdot - name;

    _tcsncpy(gname, name, cnt);
    gname[cnt] = '\0';
}
//it is very danger to use pointer with STL, so we use vector & id reference as main storage mechanism
//   *group list is only created when new data name are met
//   *data range may keep changing when new data arrived
// these are all maintained by data_manager now;
struct DataManager
{
    std::vector<data_info>                      data;           //for most basic storage & iteration (both refernced by id).
    std::map<std::string, int>                  name2id;        //based on data, provide dataname key based reference

    std::vector<group_info>                     group;
    std::map<std::string, int>                  gname2ids;      //based on data, provide groupname key based reference

    float                                       record_time;

	float										x_min;
	float										x_max;
	
	DataManager(){x_min = FLT_MAX; x_max = -FLT_MAX;record_time = 0;}
    void ticktock(float time_step_sec) { record_time += time_step_sec; }

    void clear()
    {
        group.clear();
        data.clear();
        name2id.clear();
        gname2ids.clear();
        record_time = 0;
		x_min = FLT_MAX; 
		x_max = -FLT_MAX;
    }

    void record(const TCHAR * data_name, const TCHAR * style, float x, float value)
    {
		if(x_min > x) x_min = x;
		if(x_max < x) x_max = x;

        data_entry p;
        p.time = x;
        p.value = value;

        data_info *pdi = NULL;
        group_info *pg = NULL;
        if (name2id.find(data_name) == name2id.end())
        {
            data_info dnew;
            data.push_back(dnew);

            pdi = &(data.back());
            pdi->id = data.size() - 1;
            _tcscpy(pdi->name, data_name);
            _tcscpy(pdi->style, style);
            pdi->range_min = value;
            pdi->range_max = value;

            //mapping name
            name2id[data_name] = pdi->id;

            //new data name met(rarely), time to maintain groups
            //and add new data in a groups
            TCHAR group_name[128];
            get_group_name(data_name, group_name);
            if (gname2ids.find(group_name) == gname2ids.end())
            {
                group_info gnew;
                group.push_back(gnew);
                pg = &(group.back());
                pdi->gid = group.size() - 1;
                gname2ids[group_name] = pdi->gid;

                pg->bIsDigital = true;
                pg->range_max = value;
                pg->range_min = value;
            }
            else
                pdi->gid = gname2ids[group_name];

            pg = &(group[pdi->gid]);
            pg->ids.push_back(pdi->id);     //add this new data into group
            if (pg->bIsDigital && (_tcsstr(pdi->style, _TEXT("d")) == NULL)) pg->bIsDigital = false;
        }
        else
        {
            pdi = &(data[name2id[data_name]]);
            pg = &(group[pdi->gid]);
        }
        //maintain data ranges
        if (pdi->range_min > value) pdi->range_min = value;
        if (pdi->range_max < value) pdi->range_max = value;
        if (pg->range_min > value) pg->range_min = value;
        if (pg->range_max < value) pg->range_max = value;
        pdi->data.push_back(p);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
struct DSOCoordinate
{
	Rect rc;
	float x0, x1, y0, y1;
	float scale_x, scale_y;
	RectF clip_rc;
};

struct DSOClass
{
	const TCHAR *	title;
	int				width;
	int				height;
	
	int				plot_height;
	int				plot_totalHeight;
	int				scroll_y;

    int             vmargin;
    int             hmargin;

	HANDLE	        main_thread;
	HWND	        hwnd;
    
    ULONG_PTR       gdiplusToken;
    Bitmap         * pmemBitmap;
    CachedBitmap   * pcachedBitmap;
	HANDLE			hDirty;

    bool            bOpened;

    Font           * pfontAnnot;
	Font           * pfontAnnotVal;
    Font           * pfontDigital;
    Font           * pfontTicksY;
    Font           * pfontTicksX;

    CRITICAL_SECTION critical_sec_data;
    ///////////////////////////////////////////////////////////////////////////////
    //data recording interface 
    DataManager      data_manager;
    void reset_inner_timer(float t){ data_manager.record_time = t; }
    void record(const TCHAR * data_name, const TCHAR * style, float x, float value){
        ::EnterCriticalSection(&critical_sec_data);
		if(x == FLT_MAX || x== -FLT_MAX) x = data_manager.record_time;
        data_manager.record(data_name, style, x, value);
		time_x0 = data_manager.x_min;
		time_x1 = data_manager.x_max;
        ::LeaveCriticalSection(&critical_sec_data);		

		::SetEvent(hDirty);
        //::InvalidateRect(hwnd, NULL, FALSE);
    }

	DWORD last_invalidate_systime;
	void ticktock(float time_step_sec) 
	{
        ::EnterCriticalSection(&critical_sec_data);
        data_manager.ticktock(time_step_sec);
		time_x0 = data_manager.x_min;
		time_x1 = data_manager.x_max;
		::LeaveCriticalSection(&critical_sec_data);

		::SetEvent(hDirty);

		DWORD systime = GetTickCount();
		DWORD delta = systime - last_invalidate_systime;

		if (delta > 1000)
        {
			last_invalidate_systime = systime;
            ::InvalidateRect(hwnd, NULL, FALSE);
        }
	}
    ///////////////////////////////////////////////////////////////////////////////

    DSOClass(const TCHAR * ptitle, int plot_width, int plot_height);
    ~DSOClass();
    void close(bool bWait);
    void update(Graphics &graphics);

    bool b_in_display;
	void display(HDC hdc, PAINTSTRUCT *pps);
	
    float			scale_time;
    float           time_x0;
    float           time_x1;
	float	log_x1;
    float           time_cursor;
    float           time_down;

	DSOCoordinate	cc;
	
	void set_coord(Graphics &graphics, Rect &rc, float x0, float x1,float y0, float y1);
    void draw_digital(Graphics &graphics, data_info & di, int id);
	void draw_curve(Graphics &graphics, data_info & di, int id);

    void magnify(float time_center, int delta);
    void magnify_min(void);
    void settime(float x_set);
    float x2time(int xPos);
    void setcursor(float x_set);

	static unsigned __stdcall Main(void* param);
	static LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
};

DSOClass::DSOClass(const TCHAR * ptitle, int plot_width, int config_height)
{
    InitializeCriticalSection(&critical_sec_data);

    title = ptitle;
    height = width = plot_width;

	plot_height = config_height;
	plot_totalHeight = 0;
	scroll_y = 0;

    vmargin = 10;
    hmargin = 80;

    time_x0 = time_x1 = time_cursor = 0;

	last_invalidate_systime = GetTickCount();

    b_in_display = false;

log_x1 = -9876;
    pmemBitmap = NULL;
    pcachedBitmap = NULL;

    hDirty = CreateEvent(NULL, FALSE, FALSE, _TEXT("Dirty"));
	
 	// Initialize GDI+.
	GdiplusStartupInput  gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    pfontAnnot = new Font(L"Arial", 14, FontStyleItalic, UnitPixel);
	pfontAnnotVal = new Font(L"Arial", 14, FontStyleRegular, UnitPixel);
    pfontDigital = new Font(L"Arial", 12, FontStyleRegular, UnitPixel);
    pfontTicksY = new Font(L"Consolas", 14, FontStyleRegular, UnitPixel);
    pfontTicksX = new Font(L"Consolas", 12, FontStyleRegular, UnitPixel);

	main_thread = (HANDLE)_beginthreadex(NULL, 0, Main, this, 0, NULL);
	scale_time = 1.0;
    data_manager.clear();

    bOpened = true;
}
DSOClass::~DSOClass()
{
    close(true);
}

void DSOClass::close(bool bWait)
{
    if (bWait) ::WaitForSingleObject(main_thread, INFINITE);

	DWORD thid = ::GetThreadId(main_thread);
	PostThreadMessage(thid, WM_QUIT,0,0);
    
    ::WaitForSingleObject(main_thread, INFINITE);

    delete pfontAnnot;
	delete pfontAnnotVal;
    delete pfontDigital;
    delete pfontTicksY;
    delete pfontTicksX;
	Gdiplus::GdiplusShutdown(gdiplusToken);

    DeleteCriticalSection(&critical_sec_data);
	CloseHandle(hDirty);

    title = NULL;
    bOpened = false;
}

/*
	the transform in GDI+ will deform shapes, so is not suitable for our purpose
	so we have to do our own transform for x-scale and y-scale.
*/
#define FATAL_ERROR(str) do{_tprintf(_TEXT("Line %d: %s\r\n"),__LINE__, str);_exit(0);}while(0)
static int generate_ticks(float x0, float x1, int expect_tick_cnt, vector<float> &ticks)
{
    ticks.clear();
	if(expect_tick_cnt == 0) return 0;

    //ticks should be as close as possible to the interval of 10^N
    float expect_resolution = (x1 - x0) / expect_tick_cnt;
    float fN = log10f(expect_resolution);
    int N = fN < 0 ? (int)(fN - 0.99f) : (int)(fN); //rounding to the lower bound

	float tick_step = pow(10.0f, N);
	if(tick_step == 0) return 0;

    if (tick_step >= expect_resolution) tick_step *= 1;
    else if (2*tick_step >= expect_resolution) tick_step *= 2;
    else if (5 * tick_step >= expect_resolution) tick_step *= 5;
	else if (10 * tick_step >= expect_resolution) {
		N ++;
		tick_step *= 10;
	}
	else 
		FATAL_ERROR("");
	
	float x = (int)(x0 / tick_step) * tick_step;
	for (; x < x1; x += tick_step)
		if (x > x0) ticks.push_back(x);
    return N;
}

void DSOClass::set_coord(Graphics &graphics, Rect &rc, 
						  float x0, float x1,
						  float y0, float y1)
{
	cc.rc = rc;
	cc.y0 = y0;
	cc.y1 = y1;
	cc.x0 = x0;
	cc.x1 = x1;
	cc.scale_y = -(float)rc.Height/(y1-y0);
	cc.scale_x = (float)rc.Width/(x1-x0);
	cc.clip_rc = RectF(cc.x0*cc.scale_x, cc.y1*cc.scale_y, (cc.x1-cc.x0)*cc.scale_x, -(cc.y1-cc.y0)*cc.scale_y);
	
	graphics.ResetTransform();
	graphics.ResetClip();

	// (y1 - y)/(y1-y0) = (py - rc.Y)/rc.Height
	// (y - y1)*scale_y = (py - rc.Y);
	// so when y = 0, py = rc.Y - y1*scale_y
	//
	graphics.TranslateTransform(cc.rc.X - cc.x0*cc.scale_x, cc.rc.Y - cc.y1*cc.scale_y);
	graphics.ScaleTransform(1, 1);
	
	Pen pen(Color::Black, 1.0);
	REAL dashValues[4] = {1, 2, 1, 2};
	//pen.SetDashPattern(dashValues, 4);

    //Draw the ticks(with number): 
    StringFormat stringformat;
    stringformat.SetAlignment(StringAlignmentFar);
    stringformat.SetLineAlignment(StringAlignmentCenter);
    graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
    SolidBrush brush(Color(255, 100, 100, 100));
    RectF txtBox;
    graphics.MeasureString(L"99.90", 4, pfontTicksY, RectF(), &stringformat, &txtBox);
    txtBox.X = cc.x0*cc.scale_x - hmargin;
	txtBox.Width =(REAL) hmargin;

    //How many ticks can we display without overlapping
    vector<float> ticks;
    int N = generate_ticks(cc.y0, cc.y1, cc.rc.Height/txtBox.Height, ticks);

    WCHAR strinfo[32];
    for (int i = 0; i < ticks.size(); i++)
    {
        txtBox.Y = ticks[i] * cc.scale_y - txtBox.Height / 2;
        if (N >= 0)  swprintf(strinfo, L"%.0f", ticks[i]);
        else if (N >= -1) swprintf(strinfo, L"%.1f", ticks[i]);
        else if (N >= -2) swprintf(strinfo, L"%.2f", ticks[i]);
        else if (N >= -3) swprintf(strinfo, L"%.3f", ticks[i]);
        else if (N >= -4) swprintf(strinfo, L"%.4f", ticks[i]);
        else swprintf(strinfo, L"%.05g", ticks[i]);
        graphics.DrawString(strinfo, wcslen(strinfo), pfontTicksY, txtBox, &stringformat, &brush);
        graphics.DrawLine(&pen, PointF(cc.x0*cc.scale_x, ticks[i] * cc.scale_y), PointF(cc.x0*cc.scale_x + 5, ticks[i] * cc.scale_y));
    }
    
    stringformat.SetAlignment(StringAlignmentCenter);
    graphics.MeasureString(L" 99.90 ", 7, pfontTicksX, RectF(), &stringformat, &txtBox);
    N = generate_ticks(cc.x0, cc.x1, (cc.rc.Width / txtBox.Width), ticks);
    txtBox.Y = cc.y0*cc.scale_y;
    for (int i = 0; i < ticks.size(); i++)
    {
        txtBox.X = ticks[i] * cc.scale_x - txtBox.Width / 2;
        if (N >= 0)  swprintf(strinfo, L"%.0f", ticks[i]);
        else if (N >= -1) swprintf(strinfo, L"%.1f", ticks[i]);
        else if (N >= -2) swprintf(strinfo, L"%.2f", ticks[i]);
        else swprintf(strinfo, L"%.05g", ticks[i]);
        graphics.DrawString(strinfo, wcslen(strinfo), pfontTicksX, txtBox, &stringformat, &brush);
        graphics.DrawLine(&pen, PointF(ticks[i] * cc.scale_x, cc.y0*cc.scale_y), PointF(ticks[i] * cc.scale_x, cc.y0*cc.scale_y - 5));
    }

	//txtBox.X = cc.x1 * cc.scale_x - txtBox.Width / 2;
	//swprintf(strinfo, L"%.2f", x1);
	//graphics.DrawString(strinfo, wcslen(strinfo), pfontTicksX, txtBox, &stringformat, &brush);

	// Draw the outline of the region.
	graphics.DrawRectangle(&pen, cc.clip_rc);
}

static const ARGB argb_table[] = {
    Color::Red,         Color::Lime,        Color::Blue,            Color::Brown, 
    Color::DarkGreen,   Color::DarkBlue,    Color::DeepSkyBlue,     Color::DeepPink,
    Color::BlueViolet, Color::DarkCyan, Color::DarkGoldenrod, Color::Purple,
    Color::BurlyWood, Color::Cornsilk, Color::Orange, Color::Pink, Color::AliceBlue, Color::LawnGreen, Color::LightPink, Color::LightGreen, Color::LightBlue,
};

void DSOClass::draw_digital(Graphics &graphics, data_info & di, int id)
{
    WCHAR strinfo[128];
    StringFormat stringformat;
    stringformat.SetAlignment(StringAlignmentCenter);
    stringformat.SetLineAlignment(StringAlignmentCenter);
    Color c(argb_table[0]);
    SolidBrush BgBrush(c);
    SolidBrush FgBrush(c);

	int i0,i1;
	di.data_id_range(time_x0, time_x1, i0,i1);
	
	graphics.SetClip(cc.clip_rc);
    float fCursorValue = di.data[i0].value;
    for (unsigned int i = i0; i <= i1 + 1; i++)
    {
        if ((i == di.data.size()) ||
            (i > 0 && di.data[i].value != di.data[i - 1].value) ||
            (di.data[i].time > cc.x1))
        {
            int data = (int)(di.data[i0].value);
            //draw data from i0 to(i-1)
            int cid = data % (sizeof(argb_table) / sizeof(argb_table[0]));
            BgBrush.SetColor(Color(argb_table[cid]));
            FgBrush.SetColor(Color((~argb_table[cid]) | Color::AlphaMask));
            float w;
            if (i < di.data.size()) w = (di.data[i].time - di.data[i0].time) * cc.scale_x;
			else w = (data_manager.x_max - di.data[i0].time) * cc.scale_x;
            RectF rc(di.data[i0].time * cc.scale_x, (cc.y0 + id + 1) * cc.scale_y, w, -1.0f * cc.scale_y);
            graphics.FillRectangle(&BgBrush, rc);
            swprintf(strinfo, L"%d", data);
            graphics.DrawString(strinfo, wcslen(strinfo), pfontDigital, rc, &stringformat, &FgBrush);
            i0 = i;
        }
        if (i<di.data.size() && di.data[i].time <= time_cursor) fCursorValue = di.data[i].value;
    }
    graphics.ResetClip();

	SolidBrush AnnotBgBrush(Color(100, 255,255,255));
    RectF txtBox;
	SolidBrush ValueBgBrush(Color::Gray);
	SolidBrush ValueFgBrush(Color::White);

    //draw data cursor
	float time_cursor_lc = time_cursor;
    bool bDrawCursor = (time_cursor_lc > cc.x0 && time_cursor_lc < cc.x1);
    if (bDrawCursor)
    {
		Pen penCursor(Color::Gray, 1);
        graphics.DrawLine(&penCursor, PointF(time_cursor_lc*cc.scale_x, cc.y0*cc.scale_y), PointF(time_cursor_lc*cc.scale_x, cc.y1*cc.scale_y));

		swprintf(strinfo, L" %.2f", time_cursor_lc);
		graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnotVal, RectF(), &stringformat, &txtBox);
		txtBox.X = time_cursor_lc * cc.scale_x - txtBox.Width/2;
		txtBox.Y = cc.y0 * cc.scale_y;
		graphics.FillRectangle(&ValueBgBrush, txtBox);
		graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnotVal, txtBox, &stringformat, &ValueFgBrush);
    }
#ifdef _UNICODE
    _tcscpy(strinfo, di.name);
#else
    mbstowcs(strinfo, di.name, 128);
#endif
    graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnot, RectF(), &stringformat, &txtBox);
    txtBox.X = cc.x1 * cc.scale_x - txtBox.Width;
    txtBox.Y = (cc.y0 + id + 1) * cc.scale_y;
	graphics.FillRectangle(&AnnotBgBrush, txtBox);
    graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnot, txtBox, &stringformat, &FgBrush);
	
    if (bDrawCursor)
    {
        swprintf(strinfo, L" %d", (int)(fCursorValue));
		graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnotVal, RectF(), &stringformat, &txtBox);
		txtBox.X = cc.x1 * cc.scale_x;
		txtBox.Y = (cc.y0 + id + 1) * cc.scale_y;
		graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnotVal, txtBox, &stringformat, &ValueBgBrush);
    }
}


void DSOClass::draw_curve(Graphics &graphics, data_info & di, int id)
{
	REAL dashValues[4] = { 1, 1, 1, 1 };
    const TCHAR * pcfg;
    REAL tension = 0.5;

	graphics.SetClip(cc.clip_rc);

	Pen pen(Color::Green, 1);
    pcfg = _tcsstr(di.style, _TEXT("c"));    if (pcfg) pen.SetColor(Color(argb_table[pcfg[1] - '0']));
    pcfg = _tcsstr(di.style, _TEXT("w"));    if (pcfg) pen.SetWidth(pcfg[1] - '0');
    pcfg = _tcsstr(di.style, _TEXT("."));    if (pcfg) pen.SetDashPattern(dashValues, 2);
    pcfg = _tcsstr(di.style, _TEXT("t"));    if (pcfg) tension = (pcfg[1] - '0') * 0.1f;
    Color color(0x80, 0, 0);
    pen.GetColor(&color);

    float time_cursor_lc = -999990;
	int i0,i1,cnt;
	di.data_id_range(time_x0, time_x1, i0,i1);
	cnt = i1-i0+1;

    PointF * curvePoints = new PointF[cnt];
    float fCursorValue = 0;
    for (unsigned int i = 0; i < cnt; i++)
    {
        curvePoints[i].X = di.data[i+i0].time * cc.scale_x;
        curvePoints[i].Y = di.data[i+i0].value * cc.scale_y;
        if (di.data[i+i0].time <= time_cursor){
            time_cursor_lc = di.data[i+i0].time;
            fCursorValue = di.data[i+i0].value;
        }
    }
    
    graphics.DrawCurve(&pen, curvePoints, cnt, tension);
    if (_tcsstr(di.style, _TEXT("p")))
    {
        SolidBrush redBrush(color);
        int pw = pen.GetWidth();
        for (int i = 0; i < cnt; i++)
            graphics.FillRectangle(&redBrush, Rect(curvePoints[i].X - pw, curvePoints[i].Y - pw, pw * 2 + 1, pw * 2 + 1));
    }
    delete[]curvePoints;

	graphics.ResetClip();

	WCHAR strinfo[128];
	RectF txtBox;
    StringFormat stringformat;
    stringformat.SetAlignment(StringAlignmentCenter);
    stringformat.SetLineAlignment(StringAlignmentCenter);
    SolidBrush AnnotBrush(color);
	SolidBrush ValueBgBrush(Color::Gray);
	SolidBrush ValueFgBrush(Color::White);
    SolidBrush AnnotBgBrush(Color(200, 255,255,255));

    //draw data cursor
    bool bDrawCursor = (time_cursor_lc > cc.x0 && time_cursor_lc < cc.x1);
    if (bDrawCursor && id == 0)
    {
		Pen penCursor(Color::Gray, 1);
        pen.SetDashPattern(dashValues, 2);
        graphics.DrawLine(&penCursor, PointF(time_cursor_lc*cc.scale_x, cc.y0*cc.scale_y), PointF(time_cursor_lc*cc.scale_x, cc.y1*cc.scale_y));

		swprintf(strinfo, L" %.2f", time_cursor_lc);
		graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnotVal, RectF(), &stringformat, &txtBox);
		txtBox.X = time_cursor_lc * cc.scale_x - txtBox.Width/2;
		txtBox.Y = cc.y0 * cc.scale_y;
		graphics.FillRectangle(&ValueBgBrush, txtBox);
		graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnotVal, txtBox, &stringformat, &ValueFgBrush);
    }
#ifdef _UNICODE
    _tcscpy(strinfo, di.name);
#else
    mbstowcs(strinfo, di.name, 128);
#endif
    graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnot, RectF(), &stringformat, &txtBox);
    txtBox.X = cc.x1 * cc.scale_x - txtBox.Width;
    txtBox.Y = cc.y1 * cc.scale_y + txtBox.Height * id;
    graphics.FillRectangle(&AnnotBgBrush, txtBox);
    graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnot, txtBox, &stringformat, &AnnotBrush);
	
    if (bDrawCursor)
    {
		stringformat.SetAlignment(StringAlignmentNear);
        swprintf(strinfo, L" %.2f", fCursorValue);
		graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnotVal, RectF(), &stringformat, &txtBox);
		txtBox.X = cc.x1 * cc.scale_x;
		txtBox.Y = cc.y1 * cc.scale_y + txtBox.Height * id;
		//graphics.FillRectangle(&ValueBgBrush, txtBox);
		graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnotVal, txtBox, &stringformat, &ValueBgBrush);
    }
}


void DSOClass::update(Graphics &graphics)
{
    //graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

	SolidBrush BgBrush(Color::White);
    graphics.FillRectangle(&BgBrush, 0, 0, width, height);

    ::RECT client_rc;
    ::GetClientRect(this->hwnd, &client_rc);

#define DIGITAL_SIGNAL_HEIGHT   28
#define TOP_BOTTOM_MAGIN 10
    int x0 = client_rc.left + hmargin;
    int subrc_width = client_rc.right - client_rc.left - 2 * hmargin;
    int y = TOP_BOTTOM_MAGIN;
	EnterCriticalSection(&critical_sec_data);
	log_x1 = time_x1;
    for (int i = 0; i < data_manager.group.size(); i++)
    {
        group_info &g = data_manager.group[i];
        float range = g.range_max - g.range_min;
        float margin = (range == 0 ? 1.0f : range) * 0.05f;
		int cur_height = g.bIsDigital ? (DIGITAL_SIGNAL_HEIGHT*g.ids.size()) : plot_height;
		
		//setup coordinate and draw
		if (g.bIsDigital) 
			set_coord(graphics, Rect(x0, y + vmargin, subrc_width, cur_height), time_x0, time_x1, 0, g.ids.size());//each signal take range of 1 
        else
        {
            //re-calculate data range within current time window
            float rng_min = FLT_MAX;
            float rng_max = -FLT_MAX;
            for (unsigned int j = 0; j < g.ids.size(); j++)
            {
                data_info &di = data_manager.data[g.ids[j]];
                int i0, i1, cnt;
                di.data_id_range(time_x0, time_x1, i0, i1);
                cnt = i1 - i0 + 1;
                if (cnt > 0)
                for (unsigned int k = i0; k < i1; k++)
                {
                    if (rng_min > di.data[k].value) rng_min = di.data[k].value;
                    if (rng_max < di.data[k].value) rng_max = di.data[k].value;
                }
            }
            float rng_size = (rng_max - rng_min);
            float rng_margin = (rng_size < 0.0000001f ? 1.0f : rng_size * 0.05f);
            set_coord(graphics, Rect(x0, y + vmargin, subrc_width, plot_height), time_x0, time_x1, rng_min - rng_margin, rng_max + rng_margin);
            //set_coord(graphics, Rect(x0, y + vmargin, subrc_width, plot_height), time_x0, time_x1, g.range_min - margin, g.range_max + margin);
        }
			
        
		for (unsigned int j = 0; j < g.ids.size(); j++){
            data_info &di = data_manager.data[g.ids[j]];
            if (g.bIsDigital) 	draw_digital(graphics, di, j);
			else 				draw_curve(graphics, di, j);
        }
        y += cur_height + 2 * vmargin;
    }
	LeaveCriticalSection(&critical_sec_data);

	SCROLLINFO sinfo;
	sinfo.cbSize = sizeof(SCROLLINFO);
	sinfo.nPage = (client_rc.bottom - client_rc.top);
	sinfo.nMin =  0;
	sinfo.nMax = plot_totalHeight;
	sinfo.fMask = SIF_RANGE | SIF_PAGE;
	SetScrollInfo(hwnd, SB_VERT, &sinfo, TRUE);
}

//use cached Bitmap to increase performance
void DSOClass::display(HDC hdc, PAINTSTRUCT *pps)
{
    if (b_in_display)
    {
        printf("display() is not supposed to re-entriable!\r\n");
    }
    b_in_display = true;

	Graphics graphics(hdc);
    bool bcreat = false;
    if (WaitForSingleObject(hDirty, 0) == WAIT_OBJECT_0)
    {
        if (pmemBitmap) delete pmemBitmap;
        if (pcachedBitmap) delete pcachedBitmap;
        RECT rc;
        ::GetClientRect(hwnd, &rc);
        width = rc.right - rc.left;
        height = rc.bottom - rc.top;
		
		//estimate canvas height
		EnterCriticalSection(&critical_sec_data);
		int y = TOP_BOTTOM_MAGIN;
		for (int i = 0; i < data_manager.group.size(); i++)
		{
			group_info &g = data_manager.group[i];
			if (g.bIsDigital)  y += DIGITAL_SIGNAL_HEIGHT * g.ids.size() + 2 * vmargin;
			else y += plot_height + 2 * vmargin;
		}
		LeaveCriticalSection(&critical_sec_data);
		plot_totalHeight = y + TOP_BOTTOM_MAGIN;
		
		//canvas size should be at least as large as client area
		if(height < plot_totalHeight) height = plot_totalHeight;
        pmemBitmap = new Bitmap(width, height, &graphics);
        Status st = pmemBitmap->GetLastStatus();
        if (st != Ok)
        {
            printf("__ERR: Bitmap create failed with %d\n", st);
        }
		Graphics * pgraphics = Graphics::FromImage(pmemBitmap);
        update(*pgraphics);
		delete pgraphics;
        pcachedBitmap = new CachedBitmap(pmemBitmap, &graphics);
        st = pcachedBitmap->GetLastStatus();
        if (st != Ok)
        {
            printf("__ERR: CachedBitmap create failed with %d\n", st);
        }
        bcreat = true;
    }

	//we draw whole graph on mem bitmap, and vertical scroll is done by scroll_y here
	if (graphics.DrawCachedBitmap(pcachedBitmap, 0, -scroll_y) != Ok) 
	{
		printf("__ERR\n");
		::SetEvent(hDirty);
	}

    b_in_display = false;
}
float DSOClass::x2time(int xPos)
{
    RECT client_rc;
    ::GetClientRect(hwnd, &client_rc);

    int x0 = client_rc.left + hmargin;
    int width = client_rc.right - client_rc.left - 2 * hmargin;

    return time_x0 + (time_x1 - time_x0) * (xPos - x0) / width;
}

void DSOClass::magnify(float time_center, int zDelta)
{
    float time_0 = time_center - time_x0;
    float time_1 = time_x1 - time_center;
    float time_delta = 0.002f * zDelta;
    float time_delta0 = time_delta * time_0;
    float time_delta1 = time_delta * time_1;

	if (time_x1-time_delta1 > time_x0+time_delta0)
	{
		time_x0 += time_delta0;
		time_x1 -= time_delta1;
		if (time_x0 < data_manager.x_min) time_x0 = data_manager.x_min;
		if (time_x1 > data_manager.x_max) time_x1 = data_manager.x_max;

		::SetEvent(hDirty);
		::InvalidateRect(hwnd, NULL, FALSE);
	}
}
void DSOClass::magnify_min(void)
{
    time_x0 = data_manager.x_min;
    time_x1 = data_manager.x_max;
    ::SetEvent(hDirty);
    ::InvalidateRect(hwnd, NULL, FALSE);
}
void DSOClass::settime(float x_set)
{
	if (x_set < data_manager.x_min) x_set = data_manager.x_min;
    float fdelta_time = x_set - time_x0;
	if (time_x1 + fdelta_time > data_manager.x_max) fdelta_time = data_manager.x_max - time_x1;
    
    if (fdelta_time != 0)
    {
        time_x0 += fdelta_time;
        time_x1 += fdelta_time;
		::SetEvent(hDirty);
        ::InvalidateRect(hwnd, NULL, FALSE);
    }
}
void DSOClass::setcursor(float x_set)
{
    time_cursor = x_set;
    ::SetEvent(hDirty);
    ::InvalidateRect(hwnd, NULL, FALSE);
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
LRESULT CALLBACK DSOClass::WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam )
{
    HDC hdc ;
    PAINTSTRUCT ps;
    RECT rect;
	short fwKeys, zDelta;
    POINT pt;
    DSOClass * pdso = (DSOClass *)::GetWindowLong(hwnd, GWL_USERDATA);
	SCROLLINFO si;
	int yPos;
	float ftime;

    switch( message )
    {
    case WM_CREATE:
        break;
    case WM_PAINT:
        hdc = BeginPaint( hwnd, &ps ) ;
		pdso->display(hdc, &ps);
        EndPaint( hwnd, &ps ) ;
        break;
    case WM_LBUTTONDOWN:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        pdso->time_down = pdso->x2time(pt.x);
        break;
    case WM_MOUSEMOVE:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        if (MK_LBUTTON & wParam)
		{
			pdso->settime(pdso->time_x0 + pdso->time_down - pdso->x2time(pt.x));
		}
        if (MK_CONTROL & wParam)  pdso->setcursor(pdso->x2time(pt.x));
        break;
	case WM_VSCROLL:
		// Get all the vertial scroll bar information.
        si.cbSize = sizeof (si);
        si.fMask  = SIF_ALL;
        GetScrollInfo (hwnd, SB_VERT, &si);
		// Save the position for comparison later on.
        yPos = si.nPos;
        switch (LOWORD (wParam))
        {
        case SB_TOP:// User clicked the HOME keyboard key.
            si.nPos = si.nMin;
            break;
        case SB_BOTTOM:// User clicked the END keyboard key.
            si.nPos = si.nMax;
            break;
        case SB_LINEUP:// User clicked the top arrow.
            si.nPos -= 1;
            break;
        case SB_LINEDOWN:// User clicked the bottom arrow.
            si.nPos += 1;
            break;
        case SB_PAGEUP:// User clicked the scroll bar shaft above the scroll box.
            si.nPos -= si.nPage;
            break;
        case SB_PAGEDOWN:// User clicked the scroll bar shaft below the scroll box.
            si.nPos += si.nPage;
            break;
        case SB_THUMBTRACK:// User dragged the scroll box.
            si.nPos = si.nTrackPos;
            break;
        default:
            break; 
        }
        // Set the position and then retrieve it.  Due to adjustments
        // by Windows it may not be the same as the value set.
        si.fMask = SIF_POS;
        SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo (hwnd, SB_VERT, &si);
        // If the position has changed, scroll window and update it.
        if (si.nPos != yPos)
        {
			pdso->scroll_y = si.nPos;
			::InvalidateRect(hwnd,NULL,false);
			//we don't reply on following windows API to scroll client area

            //ScrollWindow(hwnd, 0, (yPos - si.nPos), NULL, NULL);
        }
        return 0;
		break;
	case WM_MOUSEWHEEL:
    	fwKeys = GET_KEYSTATE_WPARAM(wParam);
		zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ::ScreenToClient(hwnd, &pt);
		
		ftime = pdso->x2time(pt.x);
		//if mouse is on graph, do time scaling
		if (ftime > pdso->time_x0 && ftime < pdso->time_x1)//(MK_CONTROL & wParam) 
			pdso->magnify(ftime, zDelta);
		else
		{
			si.cbSize = sizeof (si);
			si.fMask = SIF_POS;
			GetScrollInfo (hwnd, SB_VERT, &si);
			si.nPos -= zDelta/2;
			SetScrollInfo (hwnd, SB_VERT, &si, TRUE);
			GetScrollInfo (hwnd, SB_VERT, &si);
			pdso->scroll_y = si.nPos;
			::InvalidateRect(hwnd,NULL,false);
		}
        break;
    case WM_RBUTTONDOWN:
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        pdso->magnify_min();
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 ) ;
        return 0;
	case WM_KEYDOWN:
		if(VK_ESCAPE == wParam)PostQuitMessage( 0 ) ;
		if(VK_SPACE == wParam) 
		{
			::InvalidateRect(hwnd,NULL,FALSE);
			//SetEvent(pdso->hDirty); //for debug
		}
		//if(VK_SPACE == wParam) printf("%f~%f\n", 0.0f, pdso->log_x1);
        return 0;
    case WM_SIZE:
        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        
        si.cbSize = sizeof(si);
        si.nPage = HIWORD(lParam); //change scroll page size
        si.fMask = SIF_PAGE;
        pdso->scroll_y = si.nPos;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

        // If the position has changed, scroll window and update it.
        SetEvent(pdso->hDirty);
        ::InvalidateRect(hwnd, NULL, false);
    }
    return DefWindowProc( hwnd, message, wParam, lParam ) ;
}

unsigned __stdcall DSOClass::Main(void* param)
{
    static TCHAR szAppName[128] = _TEXT("DSOWindow");
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
        //MessageBox( NULL, _TEXT("RegisterClass() failed!"), _TEXT("error"), MB_OK | MB_ICONERROR ) ;
        //return 0 ;
    }

    pthis->hwnd = CreateWindow(szAppName, pthis->title,
								WS_OVERLAPPEDWINDOW | WS_VSCROLL, 
								CW_USEDEFAULT,CW_USEDEFAULT, pthis->width, pthis->height,
								NULL,NULL,hInstance,NULL);
    
    ::SetWindowLong(pthis->hwnd, GWL_USERDATA, (LONG)pthis);

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
static std::vector<DSOClass *>			g_DSOs;
static std::map<std::string, int>		g_DSOmap;

static const TCHAR * cfg_title = TEXT("");
static int cfg_width = 600;
static int cfg_height = 200;
static bool cfg_enable = false;
void emulDSO_create(const TCHAR * title, int width, int height)
{
	cfg_title = title;
	cfg_width = width;
	cfg_height = height;
    cfg_enable = true;
}
void emulDSO_close(int waitForUser)
{
    if (!cfg_enable) return;
	emulDSO_update(NULL);
	for(int i=0;i<g_DSOs.size(); i++)
	{
		DSOClass * pDSO = g_DSOs[i];
		delete pDSO;
	}
	g_DSOs.clear();
	g_DSOmap.clear();
    cfg_enable = false;
}
void emulDSO_update(const TCHAR *dso_name)
{
    if (!cfg_enable) return;
	for(int i=0;i<g_DSOs.size(); i++)
	{
		DSOClass * pDSO = g_DSOs[i];
		//only update specified dso
		if((dso_name != NULL) && (_tcscmp(dso_name, pDSO->title)!=0)) continue;
		::SetEvent(pDSO->hDirty);
		::InvalidateRect(pDSO->hwnd, NULL, FALSE);
	}
}
static DSOClass * _emulDSO_find_DSO(const TCHAR * pdso_name)
{
    if (!cfg_enable) return NULL;
    DSOClass * pDSO;
    if (pdso_name == NULL) pdso_name = _TEXT("");
    if (g_DSOmap.find(pdso_name) == g_DSOmap.end())
    {
        //create the DSO
        pDSO = new DSOClass(pdso_name, cfg_width, cfg_height);
        g_DSOs.push_back(pDSO);
        g_DSOmap[pdso_name] = g_DSOs.size() - 1;
    }
    else pDSO = g_DSOs[g_DSOmap[pdso_name]];
    return pDSO;
}
static DSOClass * _emulDSO_find_Data(const TCHAR * data_name, TCHAR * inner_data_name)
{
    if (!cfg_enable) return NULL;
    const TCHAR * pdso_name = _tcsstr(data_name, _TEXT("@"));
    if (pdso_name == NULL)
    {
        pdso_name = _TEXT("");
        _tcscpy(inner_data_name, data_name);
    }
    else
    {
        int len = pdso_name - data_name;
        _tcsncpy(inner_data_name, data_name, len);
        inner_data_name[len] = 0;
        pdso_name++;//skip the '@'
    }
    return _emulDSO_find_DSO(pdso_name);
}
void emulDSO_record2(const TCHAR * data_name, const TCHAR * style, float x, float value)
{
    if (!cfg_enable) return;
    TCHAR inner_data_name[256];
    DSOClass * pDSO = _emulDSO_find_Data(data_name, inner_data_name);
	pDSO->record(inner_data_name, style, x, value);
}
void emulDSO_record(const TCHAR * data_name, const TCHAR * style, float value)
{
    if (!cfg_enable) return;
    TCHAR inner_data_name[256];
    DSOClass * pDSO = _emulDSO_find_Data(data_name, inner_data_name);
    pDSO->record(inner_data_name, style, FLT_MAX, value);
}

static float tick_log[256] = { 0 };
static int   tick_id = 0;
void emulDSO_record3(const TCHAR * data_name, const TCHAR * style, int tick_offset, float value)
{
    if (!cfg_enable) return;
    float x;
    TCHAR inner_data_name[256];
    DSOClass * pDSO = _emulDSO_find_Data(data_name, inner_data_name);
    x = tick_log[(tick_id + tick_offset) & 255];
    pDSO->record(inner_data_name, style, x, value);
}


void emulDSO_get_text(const TCHAR * data_name, const TCHAR * txt, 
					  int size, 
					  char * pdata, int w, int h)
{
    TCHAR inner_data_name[256];
    DSOClass * pDSO = _emulDSO_find_Data(data_name, inner_data_name);

	HDC hdc = GetDC(pDSO->hwnd);
	{
		Graphics graphics(hdc);
		WCHAR strinfo[128];
		StringFormat stringformat;
        stringformat.SetAlignment(StringAlignmentCenter);
        stringformat.SetLineAlignment(StringAlignmentCenter);
	#ifdef _UNICODE
		_tcscpy(strinfo, di.name);
	#else
		mbstowcs(strinfo, txt, 128);
	#endif

		RectF txtBox;
		Font * pfont;
        SolidBrush ValueFgBrush(Color::White);
        SolidBrush ValueBgBrush(Color::Black);
        SolidBrush ValueCleanBrush(Color::AntiqueWhite);

        txtBox.Width = pDSO->width;
		txtBox.Height = pDSO->height;
        graphics.FillRectangle(&ValueCleanBrush, txtBox);

		//pfont = new Font(L"Arial", size, FontStyleRegular, UnitPixel);
		pfont = new Font(L"Consolas", size, FontStyleRegular, UnitPixel);
		//graphics.MeasureString(strinfo, wcslen(strinfo), pfont, RectF(), &stringformat, &txtBox);
        txtBox.Width = w;
        txtBox.Height = h;
		graphics.FillRectangle(&ValueBgBrush, txtBox);
		//graphics.DrawString(strinfo, -1, pfont, txtBox, stringformat.GenericTypographic(), &ValueFgBrush);
        graphics.DrawString(strinfo, -1, pfont, txtBox, &stringformat, &ValueFgBrush);

		for(int y=0; y<h; y++)
		{
			for(int x=0; x<w; x++)
			{
				COLORREF color = ::GetPixel(hdc, x, y);
                pdata[y*w + x] = GetRValue(color);
                ::SetPixel(hdc, x + (pDSO->width - w) / 2, y + +(pDSO->height - h) / 2,
                    RGB(pdata[y*w + x], pdata[y*w + x], pdata[y*w + x]));
			}
		}

		delete pfont;
	}
	::ReleaseDC(pDSO->hwnd, hdc);
}

void emulDSO_settick(const TCHAR * dso_name, float time)
{
    if (!cfg_enable) return;
    DSOClass * pDSO = _emulDSO_find_DSO(dso_name);
    pDSO->reset_inner_timer(time);
}

void emulDSO_ticktock(const TCHAR * dso_name, float step_sec)
{
    if (!cfg_enable) return;
    DSOClass * pDSO = _emulDSO_find_DSO(dso_name);
    pDSO->ticktock(step_sec);

    //record current tick into buffer
    tick_id++;
    tick_log[tick_id & 255] = pDSO->data_manager.record_time;
}
float emulDSO_curtick(const TCHAR * dso_name)
{
    if (!cfg_enable) return 0;
    DSOClass * pDSO = _emulDSO_find_DSO(dso_name);
    return pDSO->data_manager.record_time;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////
// bitmap font lib generate support
//
static char * u8name(unsigned char data)
{
    static char name[10];
    int i;
    for (i = 0; i < 8; i++)
    {
        name[i] = (data & (1 << (7 - i))) ? 'X' : '_';
    }
    name[i] = 0;
    return name;
}
static void font_lib_output(FILE *fp, char * pgray, int w, int h)
{
    int x, y, byte, i;
    fprintf(fp, "\t{\n\t");
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            //left to right, MSB to LSB
            i = 7 - (x & 7);
            if (i == 7) byte = 0;
            byte |= pgray[x] ? (1 << i) : (0);

            if (i == 0) fprintf(fp, "%s,", u8name(byte));
        }
        fprintf(fp, "\n\t");
        pgray += w;
    }
    fprintf(fp, "\t},\n");
}
void emulDSO_generate_font(const char * out_file, const char * strFont, int w, int h)
{
    char data[128 * 128];
    char txt[2] = { 0 };
    int fsize = w * 1.45;
    int len = strlen(strFont);

    FILE * fp = fopen(out_file, "wb");
    int i;

    for (i = 0; i <= 0xFF; i++)
        fprintf(fp, "#define %s 0x%02x\n", u8name(i), i);

    fprintf(fp, "\n");

    //create the window/UI_thread and wait for initialize
    emulDSO_create("test", 600, 200);
    emulDSO_get_text("text", "0", 10, data, w, h);	Sleep(100);

    fprintf(fp, "static unsigned char digits%dx%d_font[][%d * %d / 8] =\n{\n", w, h, w, h);
    //start draw and capture
    for (i = 0; i<len; i++)
    {
        txt[0] = strFont[i];
        emulDSO_get_text("text", txt, fsize, data, w, h + h / 8);//the last (h/8) lines are empty
        fprintf(fp, "//%s\n", txt);
        font_lib_output(fp, data, w, h);
    }
    fprintf(fp, "}\n");
    fclose(fp);
    Sleep(1000);
    return;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frequency analyse support
#define pi (3.14159265)
struct complex
{
    float r;
    float i;
    complex():r(0), i(0){}
    complex(float cr, float ci):r(cr), i(ci){}

    complex get_MagPhase()                       {complex res; res.r = sqrt(r*r + i*i); res.i = atan2(i, r); return res;}
    const complex operator+(const complex& p)    {complex res; res.r = r + p.r; res.i = i + p.i; return res;}
    const complex operator-(const complex& p)    {complex res; res.r = r - p.r; res.i = i - p.i; return res;}
    const complex operator*(const complex& p)    {complex res; res.r = r*p.r - i*p.i; res.i = r*p.i + i*p.r; return res;}
};

static int bit_rev(int bits, int cnt)
{
    int rev = 0;
    for(int i=0;i<cnt;i++)
    {
        rev <<= 1;
        rev |= (bits & 1);
        bits >>=1;
    }
    return rev;
}

//there is a trick, since if (bit_rev(a)==b) then (bit_rev(b)==a)
//we exchange a and b, and if:
//      bit_rev(a) > a, we do the exchange;
//      bit_rev(a) = a, we do nothing;
//      bit_rev(a) < a, we already do the exchange before, because a is increasing, we already meet a_old = bit_rev(a);
//PSH code is even better
//      we have if bit_rev(a)==b,  then bit_rev(~a)==~b, ~a is bit-wise inverse of a
static void bit_rev_permutation(complex * x, int exponent)
{
    int j, i;
    complex temp;
    int N = 1 << exponent;
    int mask = N-1;
    for(i=0;i<N;i++)
    {
        j = bit_rev(i, exponent);
        if(j > i)
        {
            temp = x[i];
            x[i] = x[j];
            x[j] = temp;
        }
    }
}
/*
    as said in wiki: http://en.wikipedia.org/wiki/Cooley%E2%80%93Tukey_FFT_algorithm
    a single butterfly is like:
    
        X(k    ) = E(k) + exp(-2*pi*k*i/N)*O(k);
        X(k+N/2) = E(k) - exp(-2*pi*k*i/N)*O(k);

    for example: N = 8, k=0,1,2,3
    input data sequence is (even and odd seperated):  E(0) E(1) E(2) E(3)  O(0) O(1) O(2) O(3)
    output data sequence is                        :  X(0) X(1) X(2) X(3)  X(4) X(5) X(6) X(7)
*/
static void butterfly(complex * pX, int k, int Np2)
{
    complex *pEk = (pX + k);
    complex *pOk = (pX + k + Np2);
    float rad = -pi*k/Np2;
    complex twiddle_factor(cos(rad), sin(rad));
    complex temp = twiddle_factor * (*pOk);
    (*pOk) = (*pEk) - temp;
    (*pEk) = (*pEk) + temp;
}
void FFT(complex * x, int exponent)
{
    int N,j,k;
    int N_max = 1 << exponent;

	//bit reverse order permutation input so even and odd indexed data are seperated inplace
    bit_rev_permutation(x, exponent);	

	//FFT stage: controled by N, N is the group size of current stage
    for(N=2; N <= N_max; N <<= 1)
    {
		//in a stage, all data are grouped into size N, 
		//each group is composed of upper-half/lower-half,
        //butterfly will transform a pair, one from upper-half, the other from lower-half
        for(j=0; j<N_max; j += N)
        {
            //size-2*M DFT: 
            //      built from size-M result by the butterfly
            // k is the index of size-M result
            for(k=0; k<(N>>1); k++)
            {
                butterfly(x + j, k, (N>>1));
            }
        }
    }
}

//only difference between DFT/DTFT is:
//	DFT is discrete both in time and frequency domain
//	DTFT only discrete in time, in frequency domain it's continuous.
//
static void DFT_real(float * x, int N, complex * X)
{
    float norm = (1.0f/N);
    for(int k=0;k<N;k++)
    {
        complex r;
        for(int n=0;n<N;n++)
        {
            r.r += x[n] * cos(-2*pi*n*k/N);
            r.i += x[n] * sin(-2*pi*n*k/N);
        }
        r.r *= norm;
        r.i *= norm;
        X[k] = r;
    }
}
//there are only N points of samples, but we need resolution M(M>=N) Fourier transform
//so it's like we zero-extend input to length M and do DFT.
static void DTFT_real(float * x, int N, complex * X, int M)
{
    for(int k=0;k<M;k++)
    {
        complex r;
        float omega = 2*pi*k/M;
        for(int n=0;n<N;n++)
        {
            r.r += x[n] * cos(-omega*n);
            r.i += x[n] * sin(-omega*n);
        }
        X[k] = r;
    }
}
//DTFT can be implemented by DFT/FFT, simply expanding original signal with zero to fit required length
void DTFT_real_byFFT(float * x, int N, complex * X, int exponent)
{
    int points = 1<<exponent;
	//extend N points to (2^exponent) with zero
    for(int k=0;k<points;k++) 
    {
        X[k].r = (k<N)? x[k]:0;
        X[k].i = 0;
    }
    FFT(X, exponent);
}

/*
LTI system characterized by Linear Constant-Coefficient Difference Equations
        sum(ak*y[n-k]) = sum(bk*x[n-k])
    or
        y[n] = (1/a0)(sum(bk*x[n-k]) - sum_from1(ak*y[n-k]))

the transffer function for z=exp(jw):
    H(z) = (sum(bk*z^(-k)))/(sum(ak*z^(-k)))
*/
void Freqz(float * b, int bn, 
           float * a, int an, 
		   complex * X, int exponentN1)
{
    int points = 1<<exponentN1;
	float rad_off = 0;
	//N point real signal only have N/2 spectrum usefull(the other half is just a mirror)
	//but since internal implementation is not specialized for real signal, so we need full N points calculation
	//but only need to return first half of result.
    complex * pB = new complex[2*points];
    complex * pA = new complex[2*points];

    //DTFT_real(b, bn, pB, 2*points, false);
    //DTFT_real(a, an, pA, 2*points, false);
    DTFT_real_byFFT(b, bn, pB, 1+exponentN1);
    DTFT_real_byFFT(a, an, pA, 1+exponentN1);

    for(int i=0;i<points;i++)
    {
        complex &numerator = pB[i];
        complex &denominator = pA[i];
        complex mp_num = numerator.get_MagPhase();
        complex mp_den = denominator.get_MagPhase();
        X[i].r = mp_num.r / mp_den.r;
		X[i].i = mp_num.i - mp_den.i;

		//phase is very unstable when magnitude of numerator is near zero.
        if(fabs(numerator.r) < 1e-6 && fabs(numerator.i) < 1e-6)
			X[i].i = i>0?X[i-1].i:0;

		//handle phase wrapping
		if(i > 0)
		{
			float step = (X[i].i - X[i-1].i);
			if(step >= pi)
				rad_off = step > 0? -2*pi:2*pi;
		}
		X[i].i += rad_off;
		/*
		//for debug Freqz
        printf("%d: (%f,%f,%f,%f),(%f,%f,%f,%f)    (%f,%f)\n", i, 
                numerator.r, numerator.i, mp_num.r, mp_num.i, 
                denominator.r, denominator.i, mp_den.r, mp_den.i,
                X[i].r, X[i].i);
		*/
    }

    delete []pB;
    delete []pA;
}
void emulDSO_freqz(const TCHAR * dso_name, float * b, int bn, float * a, int an, int exponentN1, int use_dB)
{
    if (!cfg_enable) return;

	int points = 1<<exponentN1;
	complex * X = new complex[points];
	Freqz(b,bn,a,an, X, exponentN1);

	TCHAR mag_name[256];
	TCHAR phase_name[256];
	_stprintf(mag_name, use_dB?_TEXT("magnitude(dB)@%s"):_TEXT("magnitude@%s"), dso_name);
    _stprintf(phase_name, _TEXT("phase(degree)@%s"), dso_name);
	
	float fstep = 0.5f/points;
	for(int i=0;i<points;i++)
	{
		float r = X[i].r;
		if(use_dB)
		{
			if(r > 0)
			{
				emulDSO_record2(mag_name, _TEXT("c0"), i*fstep, 20*log10(r));
				emulDSO_record2(phase_name, _TEXT("c2"), i*fstep, X[i].i * 180 / (3.14159265f));
			}
		}
		else
		{
			emulDSO_record2(mag_name, _TEXT("c0"), i*fstep, r);
			emulDSO_record2(phase_name, _TEXT("c2"), i*fstep, X[i].i * 180 / (3.14159265f));
		}
	}
	emulDSO_update(dso_name);
}
