#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "SearchEngine.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {
constexpr int ID_PATH=100, ID_QUERY=101, ID_BROWSE=102, ID_START=103, ID_CANCEL=104, ID_RESULTS=105;
constexpr UINT WM_SEARCH_FINISH = WM_APP+2;
constexpr UINT_PTR FLUSH_TIMER=1;
HWND windowHandle{}, pathEdit{}, queryEdit{}, browseBtn{}, startBtn{}, cancelBtn{}, resultList{}, statusText{};
HFONT uiFont{};
SearchEngine engine;
std::mutex pendingMutex;
std::vector<std::wstring> pending;
std::vector<std::wstring> results;
struct FinishInfo { bool cancelled; double seconds; uint64_t scanned; uint64_t matched; };
std::wstring getText(HWND h) {
    int n=GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n)+1, L'\0');
    GetWindowTextW(h,s.data(),n+1); s.resize(n); return s;
}
void setFont(HWND h) { SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(uiFont),TRUE); }
void updateStatus(const std::wstring& s) { SetWindowTextW(statusText,s.c_str()); }
void flushMatches() {
    std::vector<std::wstring> batch;
    { std::lock_guard<std::mutex> lock(pendingMutex); batch.swap(pending); }
    if (batch.empty()) return;
    SendMessageW(resultList,WM_SETREDRAW,FALSE,0);
    for (auto& item : batch) {
        results.push_back(std::move(item));
        const auto& s=results.back();
        SendMessageW(resultList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));
    }
    SendMessageW(resultList,WM_SETREDRAW,TRUE,0);
    InvalidateRect(resultList,nullptr,FALSE);
    updateStatus(L"Searching...  Results: " + std::to_wstring(results.size()));
}
void layout(int width,int height) {
    const int m=14, row=30, gap=9;
    const int content=width-2*m;
    MoveWindow(pathEdit,m,14,content-100,row,TRUE);
    MoveWindow(browseBtn,width-m-92,14,92,row,TRUE);
    MoveWindow(queryEdit,m,14+row+gap,content-190,row,TRUE);
    MoveWindow(startBtn,width-m-182,14+row+gap,88,row,TRUE);
    MoveWindow(cancelBtn,width-m-88,14+row+gap,88,row,TRUE);
    MoveWindow(statusText,m,94,content,25,TRUE);
    MoveWindow(resultList,m,125,content,std::max(30,height-125-m),TRUE);
}
void browse() {
    BROWSEINFOW bi{}; bi.hwndOwner=windowHandle; bi.lpszTitle=L"Choose directory to search";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pid=SHBrowseForFolderW(&bi);
    if (pid) {
        wchar_t selected[MAX_PATH]{};
        if (SHGetPathFromIDListW(pid,selected)) SetWindowTextW(pathEdit,selected);
        CoTaskMemFree(pid);
    }
}
void beginSearch() {
    const auto root=getText(pathEdit), query=getText(queryEdit);
    std::error_code ec;
    if (query.empty() || !std::filesystem::is_directory(std::filesystem::path(root),ec)) {
        MessageBoxW(windowHandle,L"Enter a search term and a valid directory.",L"Invalid input",MB_OK|MB_ICONWARNING);
        return;
    }
    results.clear();
    { std::lock_guard<std::mutex> lock(pendingMutex); pending.clear(); }
    SendMessageW(resultList,LB_RESETCONTENT,0,0);
    EnableWindow(startBtn,FALSE); EnableWindow(cancelBtn,TRUE);
    updateStatus(L"Searching...");
    const bool ok=engine.start(root,query,
        [](std::wstring path) {
            { std::lock_guard<std::mutex> lock(pendingMutex); pending.push_back(std::move(path)); }
        },
        [](bool cancelled,double seconds,uint64_t scanned,uint64_t matched) {
            auto* info=new FinishInfo{cancelled,seconds,scanned,matched};
            if (!PostMessageW(windowHandle,WM_SEARCH_FINISH,0,reinterpret_cast<LPARAM>(info))) delete info;
        });
    if (!ok) { EnableWindow(startBtn,TRUE); EnableWindow(cancelBtn,FALSE); updateStatus(L"Unable to start search."); }
}
void openSelected() {
    int idx=static_cast<int>(SendMessageW(resultList,LB_GETCURSEL,0,0));
    if (idx==LB_ERR || static_cast<size_t>(idx)>=results.size()) return;
    std::wstring args=L"/select,\"" + results[idx] + L"\"";
    ShellExecuteW(windowHandle,L"open",L"explorer.exe",args.c_str(),nullptr,SW_SHOWNORMAL);
}
LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        windowHandle=hwnd;
        uiFont=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        pathEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"C:\\",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_PATH,nullptr,nullptr);
        queryEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HMENU)ID_QUERY,nullptr,nullptr);
        browseBtn=CreateWindowW(L"BUTTON",L"Browse...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,0,0,0,0,hwnd,(HMENU)ID_BROWSE,nullptr,nullptr);
        startBtn=CreateWindowW(L"BUTTON",L"Search",WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,0,0,0,0,hwnd,(HMENU)ID_START,nullptr,nullptr);
        cancelBtn=CreateWindowW(L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON|WS_DISABLED,0,0,0,0,hwnd,(HMENU)ID_CANCEL,nullptr,nullptr);
        statusText=CreateWindowW(L"STATIC",L"Ready. Double-click a result to locate it in Explorer.",WS_CHILD|WS_VISIBLE,0,0,0,0,hwnd,nullptr,nullptr,nullptr);
        resultList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,0,0,0,0,hwnd,(HMENU)ID_RESULTS,nullptr,nullptr);
        for (HWND h : {pathEdit,queryEdit,browseBtn,startBtn,cancelBtn,statusText,resultList}) setFont(h);
        SetTimer(hwnd,FLUSH_TIMER,100,nullptr);
        return 0;
    }
    case WM_SIZE: layout(LOWORD(lp),HIWORD(lp)); return 0;
    case WM_TIMER: if(wp==FLUSH_TIMER) flushMatches(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: if(HIWORD(wp)==BN_CLICKED) browse(); break;
        case ID_START: if(HIWORD(wp)==BN_CLICKED) beginSearch(); break;
        case ID_CANCEL: if(HIWORD(wp)==BN_CLICKED) { engine.cancel(); updateStatus(L"Cancelling..."); } break;
        case ID_RESULTS: if(HIWORD(wp)==LBN_DBLCLK) openSelected(); break;
        }
        return 0;
    case WM_SEARCH_FINISH: {
        std::unique_ptr<FinishInfo> info(reinterpret_cast<FinishInfo*>(lp));
        flushMatches();
        EnableWindow(startBtn,TRUE); EnableWindow(cancelBtn,FALSE);
        wchar_t s[256];
        swprintf_s(s,L"%s  |  Matches: %llu  |  Files scanned: %llu  |  Elapsed: %.2f s",
                   info->cancelled?L"Cancelled":L"Complete",
                   static_cast<unsigned long long>(info->matched),
                   static_cast<unsigned long long>(info->scanned),info->seconds);
        updateStatus(s);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd,FLUSH_TIMER);
        engine.cancel(); engine.wait();
        if(uiFont) DeleteObject(uiFont);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=instance;
    wc.lpszClassName=L"FastFileSearcherCppWindow"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    if(!RegisterClassW(&wc)) return 1;
    HWND hwnd=CreateWindowExW(0,wc.lpszClassName,L"Fast File Searcher - C++",
                              WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,900,600,
                              nullptr,nullptr,instance,nullptr);
    if(!hwnd) return 1;
    ShowWindow(hwnd,show);
    MSG msg;
    while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
