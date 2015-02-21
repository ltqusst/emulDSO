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
    const char * name;
    const char * style;
    std::vector<data_entry> data;
    float range_min;
    float range_max;
};
struct group_info
{
    bool bIsDigital;
    float range_min;
    float range_max;
    std::vector<int> ids;   //data id of this group
};
static void get_group_name(const char * name, char * gname)
{
    const char * pdot = strrchr(name, '.');
    int cnt = strlen(name);
    if (pdot)
        cnt = pdot - name;

    strncpy(gname, name, cnt);
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
    void ticktock(float time_step_sec) { record_time += time_step_sec; }

    void clear()
    {
        group.clear();
        data.clear();
        name2id.clear();
        gname2ids.clear();
        record_time = 0;
    }

    void record(const char * data_name, const char * style, float value){
        record(data_name, value, record_time, style);
    }

    void record(const char * data_name, float value, float x, const char * style)
    {
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
            pdi->name = data_name;
            pdi->style = style;
            pdi->range_min = value;
            pdi->range_max = value;

            //mapping name
            name2id[data_name] = pdi->id;

            //new data name met(rarely), time to maintain groups
            //and add new data in a groups
            char group_name[128];
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
            if (pg->bIsDigital && (strstr(pdi->style, "d") == NULL)) pg->bIsDigital = false;
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
    
    ULONG_PTR       gdiplusToken;
    Bitmap         * pmemBitmap;
    CachedBitmap   * pcachedBitmap;
    bool            bDirty;

    bool            bOpened;

    Font           * pfontAnnot;
    Font           * pfontDigital;
    Font           * pfontTicksY;
    Font           * pfontTicksX;

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

		time_x0 = 0; 
		time_x1 = data_manager.record_time;
    }
    ///////////////////////////////////////////////////////////////////////////////

    DSOClass(const char * ptitle, int plot_width, int plot_height);
    ~DSOClass();
    void close(bool bWait);
    void update(Graphics &graphics);
	void display(HDC hdc);
	
    float			scale_time;
    float           time_x0;
    float           time_x1;
    float           time_cursor;
    float           time_down;

	vector<DSOCoordinate> coord;
	
	void push_coord(Graphics &graphics, Rect &rc, float x0, float x1,float y0, float y1);
	void pop_coord(Graphics &graphics);
	void set_coord(Graphics &graphics, DSOCoordinate &cc, bool bDrawAxis = false);
    void draw_digital(Graphics &graphics, data_info & di, int id);
	void draw_curve(Graphics &graphics, data_info & di, int id);

    void magnify(float time_center, int delta);
    void settime(float x_set);
    float x2time(int xPos);
    void setcursor(float x_set);

	static unsigned __stdcall Main(void* param);
	static LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
};

DSOClass::DSOClass(const char * ptitle, int plot_width, int plot_height)
{
    InitializeCriticalSection(&critical_sec_data);

    title = ptitle;
    width = plot_width;
    height = plot_height;
    vmargin = 20;
    hmargin = 80;

    time_cursor = 0;

    pmemBitmap = NULL;
    pcachedBitmap = NULL;
    bDirty = true;

 	// Initialize GDI+.
	GdiplusStartupInput  gdiplusStartupInput;
	GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    pfontAnnot = new Font(L"Arial", 16, FontStyleItalic, UnitPixel);
    pfontDigital = new Font(L"Arial", 16, FontStyleBold, UnitPixel);
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
    delete pfontDigital;
    delete pfontTicksY;
    delete pfontTicksX;
	Gdiplus::GdiplusShutdown(gdiplusToken);

    DeleteCriticalSection(&critical_sec_data);

    title = NULL;
    bOpened = false;
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
#define FATAL_ERROR(str) do{printf("Line %d: %s\r\n",__LINE__, str);_exit(0);}while(0)
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
	PointF polyPoints[] = {PointF(cc.x0*cc.scale_x, cc.y0*cc.scale_y), PointF(cc.x1*cc.scale_x, cc.y0*cc.scale_y), 
						  PointF(cc.x1*cc.scale_x, cc.y1*cc.scale_y), PointF(cc.x0*cc.scale_x, cc.y1*cc.scale_y)};
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
	}
	graphics.SetClip(&region);
}

static const ARGB argb_table[] = {
    Color::Red,         Color::Lime,        Color::Blue,            Color::Brown, 
    Color::DarkGreen,   Color::DarkBlue,    Color::DeepSkyBlue,     Color::DeepPink,
    Color::BlueViolet, Color::DarkCyan, Color::DarkGoldenrod, Color::Purple,
    Color::BurlyWood, Color::Cornsilk, Color::Orange, Color::Pink, Color::AliceBlue, Color::LawnGreen, Color::LightPink, Color::LightGreen, Color::LightBlue,
};

void DSOClass::draw_digital(Graphics &graphics, data_info & di, int id)
{
    DSOCoordinate &cc = coord.back();
    WCHAR strinfo[128];
    StringFormat stringformat;
    stringformat.SetAlignment(StringAlignmentCenter);
    stringformat.SetLineAlignment(StringAlignmentCenter);
    Color c(argb_table[0]);
    SolidBrush BgBrush(c);
    SolidBrush FgBrush(c);
    unsigned int i0 = 0;
    while (i0 < di.data.size() && di.data[i0].time < cc.x0) i0++;
    if(i0 > 0) i0--;

    float fCursorValue = di.data[i0].value;
    for (unsigned int i = 0; i <= di.data.size(); i++)
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
            else w = (data_manager.record_time - di.data[i0].time) * cc.scale_x;
            RectF rc(di.data[i0].time * cc.scale_x, (cc.y0 + id + 1) * cc.scale_y, w, -1.0f * cc.scale_y);
            graphics.FillRectangle(&BgBrush, rc);
            swprintf(strinfo, L"%d", data);
            graphics.DrawString(strinfo, wcslen(strinfo), pfontDigital, rc, &stringformat, &FgBrush);
            i0 = i;
        }
        if (i<di.data.size() && di.data[i].time <= time_cursor) fCursorValue = di.data[i].value;
    }
    
    //draw data cursor
    bool bDrawCursor = (time_cursor > cc.x0 && time_cursor < cc.x1);
    if (bDrawCursor)
    {
        Pen penCursor(Color::Black, 1);
        graphics.DrawLine(&penCursor, PointF(time_cursor*cc.scale_x, cc.y0*cc.scale_y), PointF(time_cursor*cc.scale_x, cc.y1*cc.scale_y));
    }

    WCHAR strname[128];
    if (bDrawCursor)
    {
        mbstowcs(strname, di.name, 128);
        swprintf(strinfo, L"%s:%d", strname, (int)(fCursorValue));
    }
    else 
        mbstowcs(strinfo, di.name, 128);
    
    RectF txtBox;
    graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnot, RectF(), &stringformat, &txtBox);
    txtBox.X = cc.x1 * cc.scale_x - txtBox.Width;
    txtBox.Y = (cc.y0 + id + 1) * cc.scale_y;

    //graphics.FillRectangle(&FgBrush, txtBox);
    graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnot, txtBox, &stringformat, &FgBrush);
}


void DSOClass::draw_curve(Graphics &graphics, data_info & di, int id)
{
    DSOCoordinate &cc = coord.back();
	REAL dashValues[4] = { 1, 1, 1, 1 };
    const char * pcfg;
    Pen pen(Color::Green, 1);

	pcfg = strstr(di.style, "c");    if (pcfg) pen.SetColor(Color(argb_table[pcfg[1] - '0']));
    pcfg = strstr(di.style, "w");    if (pcfg) pen.SetWidth(pcfg[1] - '0');
    pcfg = strstr(di.style, ".");    if (pcfg) pen.SetDashPattern(dashValues, 2);
    Color color(0x80, 0, 0);
    pen.GetColor(&color);

    float time_cursor_lc = -999990;
    
    PointF * curvePoints = new PointF[di.data.size()];
    float fCursorValue = 0;
    for (unsigned int i = 0; i < di.data.size(); i++)
    {
        curvePoints[i].X = di.data[i].time * cc.scale_x;
        curvePoints[i].Y = di.data[i].value * cc.scale_y;
        if (di.data[i].time <= time_cursor){
            time_cursor_lc = di.data[i].time;
            fCursorValue = di.data[i].value;
        }
    }
    
    graphics.DrawCurve(&pen, curvePoints, di.data.size());
    if (strstr(di.style, "p"))
    {
        SolidBrush redBrush(color);
        int pw = pen.GetWidth();
        for (unsigned int i = 0; i < di.data.size(); i++)
            graphics.FillRectangle(&redBrush, Rect(curvePoints[i].X - pw, curvePoints[i].Y - pw, pw * 2 + 1, pw * 2 + 1));
    }
    delete[]curvePoints;

    //draw data cursor
    bool bDrawCursor = (time_cursor_lc > cc.x0 && time_cursor_lc < cc.x1);
    if (bDrawCursor && id == 0)
    {
        Pen penCursor(Color::Black, 1);
        pen.SetDashPattern(dashValues, 2);
        graphics.DrawLine(&penCursor, PointF(time_cursor_lc*cc.scale_x, cc.y0*cc.scale_y), PointF(time_cursor_lc*cc.scale_x, cc.y1*cc.scale_y));
    }

    //draw annotation at upper right corner?
    StringFormat stringformat;
    stringformat.SetAlignment(StringAlignmentCenter);
    stringformat.SetLineAlignment(StringAlignmentCenter);
    SolidBrush AnnotBrush(color);
    SolidBrush AnnotBgBrush(Color(200, 255,255,255));

    WCHAR strinfo[128];
    WCHAR strname[128];
    if (bDrawCursor)
    {
        mbstowcs(strname, di.name, 128);
        swprintf(strinfo, L"%s:%.2f", strname, fCursorValue);
    }
    else
        mbstowcs(strinfo, di.name, 128);

    RectF txtBox;
    graphics.MeasureString(strinfo, wcslen(strinfo), pfontAnnot, RectF(), &stringformat, &txtBox);
    txtBox.X = cc.x1 * cc.scale_x - txtBox.Width;
    txtBox.Y = cc.y1 * cc.scale_y + txtBox.Height * id;
    graphics.FillRectangle(&AnnotBgBrush, txtBox);
    graphics.DrawString(strinfo, wcslen(strinfo), pfontAnnot, txtBox, &stringformat, &AnnotBrush);

}

void DSOClass::update(Graphics &graphics)
{

    //graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush BgBrush(Color(255,255,255,255));
    graphics.FillRectangle(&BgBrush, 0, 0, width, height);



    //coordinates information is fixed
    //time_x0 = 0; time_x1 = data_manager.record_time;
    //time_x0 = 1.0; time_x1 = 2.5;// data_manager.record_time;

    EnterCriticalSection(&critical_sec_data);

    ::RECT client_rc;
    ::GetClientRect(this->hwnd, &client_rc);

#define DIGITAL_SIGNAL_HEIGHT   28
    int DynPlotCount = 0;
    int DynHeightTotal = client_rc.bottom - client_rc.top - 20;
    //iterate for height allocation
    for (int i = 0; i < data_manager.group.size(); i++)
    {
        group_info &g = data_manager.group[i];
        if (g.bIsDigital) DynHeightTotal -= DIGITAL_SIGNAL_HEIGHT * g.ids.size() + 2 * vmargin;
        else DynPlotCount ++ ;
    }

    int x0 = client_rc.left + hmargin;
    int width = client_rc.right - client_rc.left - 2 * hmargin;
    int height = DynHeightTotal / DynPlotCount - 2 * vmargin;
    int y = 10;
    for (int i = 0; i < data_manager.group.size(); i++)
    {
        group_info &g = data_manager.group[i];
        float range = g.range_max - g.range_min;
        float margin = range * 0.03f;

        //setup coordinate and draw
        if (g.bIsDigital)
        {
            push_coord(graphics, Rect(x0, y + vmargin, width, DIGITAL_SIGNAL_HEIGHT * g.ids.size()), time_x0, time_x1, 0, g.ids.size());//each signal take range of 1 
            for (unsigned int j = 0; j < g.ids.size(); j++){
                data_info &di = data_manager.data[g.ids[j]];
                draw_digital(graphics, di, j);
            }
            pop_coord(graphics);
            y += DIGITAL_SIGNAL_HEIGHT * g.ids.size() + 2 * vmargin;
        }
        else
        {
            push_coord(graphics, Rect(x0, y + vmargin, width, height), time_x0, time_x1, g.range_min - margin, g.range_max + margin);
            for (unsigned int j = 0; j < g.ids.size(); j++){
                data_info &di = data_manager.data[g.ids[j]];
                draw_curve(graphics, di, j);
            }
            pop_coord(graphics);
            y += height + 2 * vmargin;
        }
    }

    LeaveCriticalSection(&critical_sec_data);
}

//use cached Bitmap to increase performance
void DSOClass::display(HDC hdc)
{
	Graphics graphics(hdc);

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
    float time_delta0 = time_delta * time_0 / (time_0 + time_1);
    float time_delta1 = time_delta * time_1 / (time_0 + time_1);

    time_x0 += time_delta0;
    time_x1 -= time_delta1;
    if (time_x0 < 0) time_x0 = 0;
    if (time_x1 > data_manager.record_time) time_x1 = data_manager.record_time;


    bDirty = true;
    ::InvalidateRect(hwnd, NULL, FALSE);
}
void DSOClass::settime(float x_set)
{
    if (x_set < 0) x_set = 0;
    float fdelta_time = x_set - time_x0;
    if (time_x1 + fdelta_time > data_manager.record_time) fdelta_time = data_manager.record_time - time_x1;
    
    if (fdelta_time != 0)
    {
        time_x0 += fdelta_time;
        time_x1 += fdelta_time;
        bDirty = true;
        ::InvalidateRect(hwnd, NULL, FALSE);
    }
}
void DSOClass::setcursor(float x_set)
{
    time_cursor = x_set;
    bDirty = true;
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

	//此处直接使用dso全局变量
    switch( message )
    {
    case WM_CREATE:
        break;

    case WM_PAINT:
        hdc = BeginPaint( hwnd, &ps ) ;
        pdso->display(hdc);
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
        if (MK_LBUTTON & wParam)  pdso->settime(pdso->time_x0 + pdso->time_down - pdso->x2time(pt.x));
        if (MK_CONTROL & wParam)  pdso->setcursor(pdso->x2time(pt.x));
        break;
	case WM_MOUSEWHEEL:
    	fwKeys = GET_KEYSTATE_WPARAM(wParam);
		zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        pt.x = (short)LOWORD(lParam);
        pt.y = (short)HIWORD(lParam);
        ::ScreenToClient(hwnd, &pt);
        pdso->magnify(pdso->x2time(pt.x), zDelta);
        break;
    case WM_DESTROY:
        PostQuitMessage( 0 ) ;
        return 0;

	case WM_KEYDOWN:
		if(VK_ESCAPE == wParam)PostQuitMessage( 0 ) ;
        return 0;
    case WM_SIZE:
        pdso->bDirty = true;
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

    pthis->hwnd = CreateWindow(szAppName, szAppName, //pthis->title,
								WS_OVERLAPPEDWINDOW, 
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
static DSOClass  * g_pDSO;
void emulDSO_create(const char * title, int width, int height)
{
    g_pDSO = new DSOClass(title, width, height);
}
void emulDSO_close(int waitForUser)
{
    if (g_pDSO)
    {
        //g_pDSO->close(waitForUser);
        delete g_pDSO;
        g_pDSO = NULL;
    }
}
void emulDSO_record(const char * data_name, const char * style, float value)
{
    if (g_pDSO) g_pDSO->record(data_name, style, value);
}
void emulDSO_ticktock(float step_sec)
{
    if (g_pDSO) g_pDSO->data_manager.ticktock(step_sec);
}
float emulDSO_curtick(void)
{
    if (g_pDSO) return g_pDSO->data_manager.record_time;
    return 0;
}
