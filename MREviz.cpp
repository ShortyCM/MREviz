/*
    MREviz
    Copyright (C) 2026 Clayton Macleod

    This file is part of MREviz.

    MREviz is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MREviz is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MREviz. If not, see <https://www.gnu.org/licenses/>.
*/
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

#include "stats.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

struct AppState {
    int shotCount = 25;
    double meanRadius = 1.0;
    std::vector<Shot2D> shots2D;
    Stats2D s2{};
} g;

HWND gShotEdit, gParamEdit, gCalculateBtn;
bool gNeedsRecalc = true;
bool gSyncingControls = false;
bool gIsComputing = false;
int gCalcProgressPercent = 0;
int gCalcEtaSeconds = 0;

static double Clamp(double v, double lo, double hi){ return (v<lo)?lo:(v>hi?hi:v);} 

void PumpPaintMessages() {
    MSG msg{};
    while (PeekMessage(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

RECT ProgressTextRect() {
    return RECT{10, 102, 430, 124};
}

void InvalidateProgress(HWND h) {
    RECT progressRc = ProgressTextRect();
    InvalidateRect(h, &progressRc, TRUE);
}

void OnCalcProgress(int percent, int etaSeconds, void* userData) {
    HWND h = static_cast<HWND>(userData);
    gCalcProgressPercent = percent;
    gCalcEtaSeconds = etaSeconds;
    InvalidateProgress(h);
    PumpPaintMessages();
}

void Recompute(HWND h) {
    gIsComputing = true;
    gCalcProgressPercent = 0;
    gCalcEtaSeconds = 0;
    InvalidateProgress(h);
    UpdateWindow(h);

    GenerateShots2D(g.shotCount, g.meanRadius, g.shots2D);
    g.s2 = ComputeStats2D(g.shots2D);
    double p5Factor = 0.0, p95Factor = 0.0;
    if (CalculateRayleighPercentilesOnDemand(g.shotCount, p5Factor, p95Factor, OnCalcProgress, h)) {
        g.s2.p5Radius = g.meanRadius * p5Factor;
        g.s2.p95Radius = g.meanRadius * p95Factor;
    }
    gIsComputing = false;
    gCalcProgressPercent = 100;
    gCalcEtaSeconds = 0;
    gNeedsRecalc = false;
    InvalidateRect(h, nullptr, TRUE);
}

void MarkDirty(HWND h) {
    gNeedsRecalc = true;
    InvalidateRect(h, nullptr, TRUE);
}

void SyncControls(HWND h) {
    gSyncingControls = true;
    SetWindowTextA(gShotEdit, std::to_string(g.shotCount).c_str());
    std::ostringstream ss; ss<<std::fixed<<std::setprecision(3)<<g.meanRadius;
    std::string mrText = ss.str();
    while (mrText.size() > 3 && mrText.back() == '0') mrText.pop_back();
    if (!mrText.empty() && mrText.back() == '.') mrText.push_back('0');
    SetWindowTextA(gParamEdit, mrText.c_str());
    gSyncingControls = false;
    MarkDirty(h);
}



void ApplyEditValue(HWND h, int controlId, bool finalize) {
    if (gSyncingControls) return;
    if (controlId == 3) {
        char b[32]; GetWindowTextA(gShotEdit, b, 31);
        g.shotCount = (int)Clamp(std::lround(atof(b)), 2, 10000);
        if (finalize) SetWindowTextA(gShotEdit, std::to_string(g.shotCount).c_str());
        MarkDirty(h);
    }
    if (controlId == 5) {
        char b[32]; GetWindowTextA(gParamEdit, b, 31);
        g.meanRadius = Clamp(std::round(atof(b) * 1000.0) / 1000.0, 0.001, 1000.0);
        if (finalize) {
            std::ostringstream ss; ss << std::fixed << std::setprecision(3) << g.meanRadius;
            std::string mrText = ss.str();
            while (mrText.size() > 3 && mrText.back() == '0') mrText.pop_back();
            if (!mrText.empty() && mrText.back() == '.') mrText.push_back('0');
            SetWindowTextA(gParamEdit, mrText.c_str());
        }
        MarkDirty(h);
    }
}



LRESULT CALLBACK EditEnterSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        HWND parent = GetParent(hwnd);
        if (parent) {
            ApplyEditValue(parent, GetDlgCtrlID(hwnd), true);
            Recompute(parent);
        }
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

COLORREF HeatColor(int c, int maxc){
    if(maxc<=1) return RGB(0,0,255);
    double t=(double)(c-1)/(maxc-1);
    int r=(int)(255*t), b=(int)(255*(1.0-t));
    return RGB(r,0,b);
}

void Draw2D(HDC hdc, RECT rc){
    FillRect(hdc,&rc,(HBRUSH)GetStockObject(WHITE_BRUSH));
    int W=rc.right-rc.left, H=rc.bottom-rc.top;
    const int rightPadding = 10;
    const int lineExtension = 72;
    const int labelGap = 8;
    const int labelReserve = 150;
    int cy=rc.top+H/2;

    const int circleAreaRight = rc.right - rightPadding - lineExtension - labelGap - labelReserve;
    int cx = rc.left + (circleAreaRight - rc.left) / 2;
    int radiusPxX = circleAreaRight - cx - 20;
    int radiusPxY = ((H / 2) - 20);
    int radiusPx = (radiusPxX < radiusPxY) ? radiusPxX : radiusPxY;
    if (radiusPx < 20) radiusPx = 20;
    double maxShotRadius = 0.0;
    for (const auto& p : g.shots2D) {
        maxShotRadius = (maxShotRadius > std::hypot(p.x, p.y)) ? maxShotRadius : std::hypot(p.x, p.y);
    }
    const double targetA = (g.s2.meanRadius * 3.0 > g.s2.p95Radius * 1.2) ? (g.s2.meanRadius * 3.0) : (g.s2.p95Radius * 1.2);
    const double targetB = (maxShotRadius * 1.02 > 1.0) ? (maxShotRadius * 1.02) : 1.0;
    double fitRadius = (targetA > targetB) ? targetA : targetB;
    double scale = radiusPx / fitRadius;

    HPEN axis=CreatePen(PS_SOLID,1,RGB(0,0,0));
    HPEN oldAxisPen = (HPEN)SelectObject(hdc,axis);

    int rm=(int)std::lround(g.s2.meanRadius*scale);
    int rP5=(int)std::lround(g.s2.p5Radius*scale);
    int rP95=(int)std::lround(g.s2.p95Radius*scale);
    int r2mr=(int)std::lround((2.0 * g.s2.meanRadius)*scale);
    int r3mr=(int)std::lround((3.0 * g.s2.meanRadius)*scale);
    int r2P5=(int)std::lround((2.0 * g.s2.p5Radius)*scale);
    int r2P95=(int)std::lround((2.0 * g.s2.p95Radius)*scale);
    int r3P5=(int)std::lround((3.0 * g.s2.p5Radius)*scale);
    int r3P95=(int)std::lround((3.0 * g.s2.p95Radius)*scale);

    // Approximate alpha-over-white shading using grayscale: alpha 25%=191, 15%=217, 5%=242
    HBRUSH r3Brush = CreateSolidBrush(RGB(242,242,242));
    HBRUSH r2Brush = CreateSolidBrush(RGB(217,217,217));
    HBRUSH rayBrush = CreateSolidBrush(RGB(191,191,191));
    HBRUSH centerBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, r3Brush);
    HPEN oldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));

    // Apply each window as two separate bands around its MR value.
    auto FillRing = [&](int outerR, int innerR, HBRUSH brush) {
        if (outerR <= innerR) return;
        SelectObject(hdc, brush);
        Ellipse(hdc, cx-outerR, cy-outerR, cx+outerR, cy+outerR);
        SelectObject(hdc, centerBrush);
        Ellipse(hdc, cx-innerR, cy-innerR, cx+innerR, cy+innerR);
    };

    // Draw outer band first, then inner band for each window.
    FillRing((r3P95 > r3mr) ? r3P95 : r3mr, (r3P95 < r3mr) ? r3P95 : r3mr, r3Brush);
    FillRing((r3mr > r3P5) ? r3mr : r3P5, (r3mr < r3P5) ? r3mr : r3P5, r3Brush);
    FillRing((r2P95 > r2mr) ? r2P95 : r2mr, (r2P95 < r2mr) ? r2P95 : r2mr, r2Brush);
    FillRing((r2mr > r2P5) ? r2mr : r2P5, (r2mr < r2P5) ? r2mr : r2P5, r2Brush);
    FillRing((rP95 > rm) ? rP95 : rm, (rP95 < rm) ? rP95 : rm, rayBrush);
    FillRing((rm > rP5) ? rm : rP5, (rm < rP5) ? rm : rP5, rayBrush);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    LOGBRUSH ringBrush{BS_SOLID, RGB(0,0,0), 0};
    HPEN ringPen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID, 1, &ringBrush, 0, nullptr);
    LOGBRUSH mrBrush{BS_SOLID, RGB(0,0,0), 0};
    HPEN mrPen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID, 2, &mrBrush, 0, nullptr);
    SelectObject(hdc, mrPen);
    Ellipse(hdc,cx-rm,cy-rm,cx+rm,cy+rm);
    Ellipse(hdc,cx-r2mr,cy-r2mr,cx+r2mr,cy+r2mr);
    Ellipse(hdc,cx-r3mr,cy-r3mr,cx+r3mr,cy+r3mr);
    SelectObject(hdc, ringPen);
    Ellipse(hdc,cx-rP5,cy-rP5,cx+rP5,cy+rP5);
    Ellipse(hdc,cx-rP95,cy-rP95,cx+rP95,cy+rP95);
    Ellipse(hdc,cx-r2P5,cy-r2P5,cx+r2P5,cy+r2P5);
    Ellipse(hdc,cx-r2P95,cy-r2P95,cx+r2P95,cy+r2P95);
    Ellipse(hdc,cx-r3P5,cy-r3P5,cx+r3P5,cy+r3P5);
    Ellipse(hdc,cx-r3P95,cy-r3P95,cx+r3P95,cy+r3P95);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(ringPen);
    DeleteObject(mrPen);
    DeleteObject(r3Brush);
    DeleteObject(r2Brush);
    DeleteObject(rayBrush);
    SelectObject(hdc, oldAxisPen);
    DeleteObject(axis);

    const int outerRingRightX = cx + r3mr;
    const int lineEndX = outerRingRightX + lineExtension;
    const int labelTextX = lineEndX + labelGap;
    const int labelStep = 24;

    struct RingLabel { const char* name; int radius; int y; };
    RingLabel ringLabels[] = {
        {"1 MR P5", rP5, cy + 4 * labelStep},
        {"1 MR", rm, cy + 3 * labelStep},
        {"1 MR P95", rP95, cy + 2 * labelStep},
        {"2 MR P5", r2P5, cy + 1 * labelStep},
        {"2 MR", r2mr, cy + 0 * labelStep},
        {"2 MR P95", r2P95, cy - 1 * labelStep},
        {"3 MR P5", r3P5, cy - 2 * labelStep},
        {"3 MR", r3mr, cy - 3 * labelStep},
        {"3 MR P95", r3P95, cy - 4 * labelStep},
    };

    int oldBkMode = SetBkMode(hdc, TRANSPARENT);
    for (const auto& label : ringLabels) {
        int startY = label.y;
        if (startY > cy + label.radius - 1) startY = cy + label.radius - 1;
        if (startY < cy - label.radius + 1) startY = cy - label.radius + 1;
        int dy = startY - cy;
        double dx = std::sqrt((double)label.radius * label.radius - (double)dy * dy);
        int startX = cx + (int)std::lround(dx);

        SIZE labelSize{};
        GetTextExtentPoint32A(hdc, label.name, (int)lstrlenA(label.name), &labelSize);
        int adjustedLabelTextX = labelTextX;
        const int maxTextRight = rc.right - rightPadding;
        if (adjustedLabelTextX + labelSize.cx > maxTextRight) adjustedLabelTextX = maxTextRight - labelSize.cx;
        int adjustedLineEndX = adjustedLabelTextX - 3;

        MoveToEx(hdc, startX, startY, nullptr);
        if (startY != label.y) LineTo(hdc, startX, label.y);
        LineTo(hdc, adjustedLineEndX, label.y);
        TextOutA(hdc, adjustedLabelTextX, label.y - (labelSize.cy / 2), label.name, (int)lstrlenA(label.name));
    }
    SetBkMode(hdc, oldBkMode);

    std::vector<int> grid(W*H,0);
    int maxc=0;
    for(const auto&p: g.shots2D){
        int px=cx+(int)std::lround(p.x*scale), py=cy-(int)std::lround(p.y*scale);
        if(px>=rc.left&&px<rc.right&&py>=rc.top&&py<rc.bottom){
            for (int oy = 0; oy < 2; ++oy) for (int ox = 0; ox < 2; ++ox) {
                int tx = px + ox;
                int ty = py + oy;
                if (tx>=rc.left&&tx<rc.right&&ty>=rc.top&&ty<rc.bottom) {
                    int idx=(ty-rc.top)*W+(tx-rc.left);
                    maxc=std::max(maxc,++grid[idx]);
                }
            }
        }
    }
    for(int y=0;y<H;y++) for(int px=0;px<W;px++){
        int c=grid[y*W+px]; if(c>0) SetPixel(hdc,rc.left+px,rc.top+y,HeatColor(c,maxc));
    }
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_CREATE:{
        InitCommonControls();
        CreateWindowA("STATIC","Shots (2-10000)",WS_CHILD|WS_VISIBLE,10,40,120,20,h,0,0,0);
        gShotEdit=CreateWindowA("EDIT","",WS_CHILD|WS_VISIBLE|WS_BORDER,130,40,120,22,h,(HMENU)3,0,0);
        SetWindowSubclass(gShotEdit, EditEnterSubclassProc, 0, 0);
        CreateWindowA("STATIC","MR (0.001-1000)",WS_CHILD|WS_VISIBLE,10,75,120,20,h,0,0,0);
        gParamEdit=CreateWindowA("EDIT","",WS_CHILD|WS_VISIBLE|WS_BORDER,130,75,120,22,h,(HMENU)5,0,0);
        SetWindowSubclass(gParamEdit, EditEnterSubclassProc, 0, 0);
        gCalculateBtn=CreateWindowA("BUTTON","Calculate",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,230,100,100,24,h,(HMENU)7,0,0);
        SyncControls(h);
        Recompute(h);
        break;}
    case WM_COMMAND:
        if(LOWORD(w)==7){ Recompute(h); }
        if(HIWORD(w)==EN_CHANGE){
            ApplyEditValue(h, LOWORD(w), false);
        }
        if(HIWORD(w)==EN_KILLFOCUS){
            ApplyEditValue(h, LOWORD(w), true);
        }
        break;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC hdc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);
        int graphSide = 1000;
        const int innerMin = ((rc.right - 20) < (rc.bottom - 140)) ? (rc.right - 20) : (rc.bottom - 140);
        if (innerMin < graphSide) graphSide = innerMin;
        RECT graph{
            10 + ((rc.right - 20) - graphSide) / 2,
            130 + ((rc.bottom - 140) - graphSide) / 2,
            10 + ((rc.right - 20) - graphSide) / 2 + graphSide,
            130 + ((rc.bottom - 140) - graphSide) / 2 + graphSide
        };
        Draw2D(hdc,graph);
        std::ostringstream line1, line2, line3, line4;
        line1<<std::fixed<<std::setprecision(3)
             <<"1MR="<<g.s2.meanRadius<<" 1MR P5="<<g.s2.p5Radius<<" 1MR P95="<<g.s2.p95Radius;
        line2<<std::fixed<<std::setprecision(3)
             <<"2MR="<<(2.0 * g.s2.meanRadius)<<" 2MR P5="<<(2.0 * g.s2.p5Radius)<<" 2MR P95="<<(2.0 * g.s2.p95Radius);
        line3<<std::fixed<<std::setprecision(3)
             <<"3MR="<<(3.0 * g.s2.meanRadius)<<" 3MR P5="<<(3.0 * g.s2.p5Radius)<<" 3MR P95="<<(3.0 * g.s2.p95Radius);
        line4 << "P5-P95 regions are the error window for " << g.shotCount << " shots.";
        if (gNeedsRecalc) {
            line1 << " (pending calculate)";
            line2 << " (pending calculate)";
            line3 << " (pending calculate)";
            line4 << " (pending calculate)";
        }
        TextOutA(hdc,500,35,line1.str().c_str(),(int)line1.str().size());
        TextOutA(hdc,500,55,line2.str().c_str(),(int)line2.str().size());
        TextOutA(hdc,500,75,line3.str().c_str(),(int)line3.str().size());
        TextOutA(hdc,500,95,line4.str().c_str(),(int)line4.str().size());

        RECT progressRc = ProgressTextRect();
        FillRect(hdc, &progressRc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        if (gIsComputing) {
            std::ostringstream progress;
            progress << "Calculating: " << gCalcProgressPercent << "%";
            if (gCalcProgressPercent < 100) {
                progress << " (ETA " << gCalcEtaSeconds << "s)";
            }
            TextOutA(hdc,10,105,progress.str().c_str(),(int)progress.str().size());
        }

        const char* probabilityLines[] = {
            "17.8% of shots will land within 0.5 MR",
            "35.7% of shots will land within 0.75 MR",
            "54.4% of shots will land within 1 MR",
            "70.6% of shots will land within 1.25 MR",
            "82.9% of shots will land within 1.5 MR",
            "90.9% of shots will land within 1.75 MR",
            "95.7% of shots will land within 2 MR",
            "98.1% of shots will land within 2.25 MR",
            "99.3% of shots will land within 2.5 MR",
            "99.9% of shots will land within 3 MR",
        };
        const int probabilityLineCount = (int)(sizeof(probabilityLines) / sizeof(probabilityLines[0]));
        TEXTMETRICA tm{};
        GetTextMetricsA(hdc, &tm);
        const int lineHeight = tm.tmHeight + tm.tmExternalLeading;
        const int blockHeight = probabilityLineCount * lineHeight;
        const int bottomPadding = 12;
        const int rightPadding = 12;
        int textY = rc.bottom - bottomPadding - blockHeight;
        for (int i = 0; i < probabilityLineCount; ++i) {
            const char* line = probabilityLines[i];
            int lineLen = (int)lstrlenA(line);
            SIZE textSize{};
            GetTextExtentPoint32A(hdc, line, lineLen, &textSize);
            int textX = rc.right - rightPadding - textSize.cx;
            TextOutA(hdc, textX, textY + i * lineHeight, line, lineLen);
        }
        EndPaint(h,&ps); break;}
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(h,m,w,l);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hi,HINSTANCE,LPSTR,int n){
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=L"MREviz";
    wc.hIcon = LoadIcon(hi, MAKEINTRESOURCE(IDI_APPICON));
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    HWND h=CreateWindowW(L"MREviz",L"Mean Radius Error Visualizer",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,1220,1240,0,0,hi,0);
    SetWindowTextW(h, L"Mean Radius Error Visualizer");
    ShowWindow(h,n);
    MSG msg; while(GetMessage(&msg,nullptr,0,0)){ TranslateMessage(&msg); DispatchMessage(&msg);} return (int)msg.wParam;
}
