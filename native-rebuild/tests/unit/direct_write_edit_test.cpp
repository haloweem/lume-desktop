#include "platform/windows/widget_ui/direct_write_edit.h"
#include <commctrl.h>
#include <imm.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
using namespace lume::platform::windows;
namespace {
int failures=0;
void Check(bool ok,const char* label){if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
std::wstring Text(HWND w){std::wstring s(static_cast<size_t>(GetWindowTextLengthW(w))+1,L'\0');s.resize(static_cast<size_t>(GetWindowTextW(w,s.data(),static_cast<int>(s.size()))));return s;}
struct Dib {
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;std::uint32_t* pixels=nullptr;int width,height;
    Dib(int w,int h):width(w),height(h){BITMAPINFO b{};b.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);b.bmiHeader.biWidth=w;b.bmiHeader.biHeight=-h;b.bmiHeader.biPlanes=1;b.bmiHeader.biBitCount=32;void* p=nullptr;bitmap=CreateDIBSection(dc,&b,DIB_RGB_COLORS,&p,nullptr,0);pixels=static_cast<std::uint32_t*>(p);old=SelectObject(dc,bitmap);}
    ~Dib(){SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);}
};
LRESULT CALLBACK Parent(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==WM_CTLCOLOREDIT||m==WM_CTLCOLORSTATIC||m==WM_CTLCOLORBTN){SetTextColor(reinterpret_cast<HDC>(wp),RGB(255,255,255));SetBkColor(reinterpret_cast<HDC>(wp),RGB(0,0,0));return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));}return DefWindowProcW(w,m,wp,lp);}
void Paint(HWND edit,int width,int height){Dib d(width,height);PatBlt(d.dc,0,0,width,height,BLACKNESS);SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(d.dc),PRF_CLIENT|PRF_ERASEBKGND);GdiFlush();int ink=0,fringe=0,smooth=0;for(int i=0;i<width*height;++i){const auto c=d.pixels[i];const int r=(c>>16)&255,g=(c>>8)&255,b=c&255;ink+=r+g+b>0;fringe+=std::max({r,g,b})-std::min({r,g,b})>2;smooth+=r>0&&r<255;}Check(ink>20,"print draws text");Check(fringe==0,"DirectWrite grayscale text has no colored fringes");Check(smooth>10,"text retains grayscale antialiasing");InvalidateRect(edit,nullptr,FALSE);UpdateWindow(edit);}
void CheckSelectionAndClip(HWND edit,int width,int height){
    const auto style=GetWindowLongPtrW(edit,GWL_STYLE);SetWindowLongPtrW(edit,GWL_STYLE,style|ES_NOHIDESEL);
    SendMessageW(edit,EM_SETSEL,1,5);Dib selected(width,height);SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(selected.dc),PRF_CLIENT);GdiFlush();
    int colored=0;for(int i=0;i<width*height;++i){const auto pixel=selected.pixels[i];const int r=(pixel>>16)&255,g=(pixel>>8)&255,b=pixel&255;colored+=std::max({r,g,b})-std::min({r,g,b})>20;}Check(colored>20,"selection background painted from DirectWrite range geometry");
    SendMessageW(edit,EM_SETSEL,0,0);Dib clipped(width+40,height+20);std::fill_n(clipped.pixels,(width+40)*(height+20),0x00335577u);
    SetViewportOrgEx(clipped.dc,20,10,nullptr);IntersectClipRect(clipped.dc,0,0,width/2,height);SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(clipped.dc),PRF_CLIENT);GdiFlush();
    bool outside=true;for(int y=0;y<height+20;++y)for(int x=0;x<width+40;++x)if(x<20||x>=20+width/2||y<10||y>=10+height)outside=outside&&clipped.pixels[y*(width+40)+x]==0x00335577u;
    Check(outside,"DirectWrite print honors HDC viewport and clipping");SetWindowLongPtrW(edit,GWL_STYLE,style);
}
void CheckLines(HWND edit){
    const std::wstring content=L"长段中文 English wrapping text 0123456789 Chinese 中文 wrapping and more characters ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    SetWindowTextW(edit,content.c_str());const auto count=SendMessageW(edit,EM_GETLINECOUNT,0,0);Check(count>1,"DirectWrite soft wrap creates multiple display lines");std::wstring reconstructed;
    for(LRESULT i=0;i<count;++i){
        const auto start=SendMessageW(edit,EM_LINEINDEX,static_cast<WPARAM>(i),0);
        Check(start==static_cast<LRESULT>(reconstructed.size()),"line indexes match DirectWrite display lines");
        Check(SendMessageW(edit,EM_LINEFROMCHAR,static_cast<WPARAM>(start),0)==i,"line from character uses DirectWrite line breaks");
        const auto length=SendMessageW(edit,EM_LINELENGTH,static_cast<WPARAM>(start),0);wchar_t line[512]{};*reinterpret_cast<WORD*>(line)=511;
        const auto copied=SendMessageW(edit,EM_GETLINE,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(line));Check(copied==length,"get line agrees with line length");reconstructed.append(line,static_cast<size_t>(copied));
    }
    Check(reconstructed==content,"soft wrap extraction preserves every UTF16 character");
}
void CheckVerticalCenter(HWND edit,int width,int height){
    SetWindowTextW(edit,L"HIM");SendMessageW(edit,EM_SETSEL,0,0);
    Dib d(width,height);SendMessageW(edit,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(d.dc),PRF_CLIENT);GdiFlush();
    int top=height,bottom=-1;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        if((d.pixels[y*width+x]&255)>30){top=std::min(top,y);bottom=std::max(bottom,y);}
    Check(bottom>=top&&std::abs(top+bottom-(height-1))<=4,"single-line text vertically centered in full client height");
    SetWindowTextW(edit,L"中文内容 ABC 123");
}
HWND menuEditor=nullptr;
bool menuSeen=false,menuKeptFocus=false;
enum class MenuAction { SelectAll, Deactivate, ReleaseCapture, DestroyEditor };
MenuAction menuAction=MenuAction::SelectAll;
BOOL CALLBACK DriveMenu(HWND window,LPARAM){
    wchar_t name[64]{};GetClassNameW(window,name,64);
    if(wcscmp(name,L"LumeWidgetMenuWindow")!=0||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId())return TRUE;
    menuSeen=true;menuKeptFocus=GetFocus()==menuEditor;
    if(menuAction==MenuAction::Deactivate){PostMessageW(window,WM_ACTIVATEAPP,FALSE,0);return FALSE;}
    if(menuAction==MenuAction::ReleaseCapture){ReleaseCapture();return FALSE;}
    if(menuAction==MenuAction::DestroyEditor){DestroyWindow(menuEditor);return FALSE;}
    PostMessageW(window,WM_KEYDOWN,VK_END,0);PostMessageW(window,WM_KEYDOWN,VK_RETURN,0);return FALSE;
}
void CALLBACK MenuTimer(HWND,UINT,UINT_PTR,DWORD){EnumThreadWindows(GetCurrentThreadId(),DriveMenu,0);}
void CheckMenu(HWND edit,MenuAction action=MenuAction::SelectAll){
    menuAction=action;
    std::cout<<"menu lifecycle "<<static_cast<int>(action)<<std::endl;
    menuEditor=edit;menuSeen=false;menuKeptFocus=false;SetFocus(edit);SendMessageW(edit,EM_SETSEL,0,0);
    const auto timer=SetTimer(nullptr,0,30,MenuTimer);const auto watchdog=SetTimer(nullptr,0,10000,[](HWND,UINT,UINT_PTR id,DWORD){KillTimer(nullptr,id);EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM)->BOOL{wchar_t name[64]{};GetClassNameW(window,name,64);if(wcscmp(name,L"LumeWidgetMenuWindow")==0)PostMessageW(window,WM_CANCELMODE,0,0);return TRUE;},0);Check(false,"menu lifecycle completed before watchdog");});
    SendMessageW(edit,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(edit),MAKELPARAM(-1,-1));KillTimer(nullptr,timer);
    KillTimer(nullptr,watchdog);
    if(action==MenuAction::DestroyEditor){
        Check(menuSeen&&!IsWindow(edit),"destroying editor exits context menu without using freed state");menuEditor=nullptr;return;
    }
    DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    Check(menuSeen,"edit context menu uses DirectWrite LumeWidgetMenu");Check(menuKeptFocus,"edit menu preserves input focus");
    if(action==MenuAction::SelectAll)Check(start==0&&end==static_cast<DWORD>(GetWindowTextLengthW(edit)),"edit menu Select All retains native selection");
    else Check(start==0&&end==0,"cancelled context menu preserves selection");
    menuEditor=nullptr;
}
}
int main(){
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_STANDARD_CLASSES};InitCommonControlsEx(&init);
    WNDCLASSW wc{};wc.lpfnWndProc=Parent;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LumeDirectWriteEditTest";RegisterClassW(&wc);
    const auto owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE,-32000,-32000,900,600,nullptr,nullptr,wc.hInstance,nullptr);
    for(const int dpi:{96,120,144,192}){
        const int width=MulDiv(260,dpi,96),height=MulDiv(34,dpi,96);
    const auto font=CreateFontW(-MulDiv(17,dpi,96),0,0,0,400,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        const auto edit=CreateWindowExW(0,L"EDIT",L"中文内容 ABC 123",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,0,0,width,height,owner,nullptr,wc.hInstance,nullptr);
#ifndef DIRECT_WRITE_EDIT_BASELINE
        Check(direct_write_edit::Attach(edit),"attach edit");Check(direct_write_edit::Attach(edit),"attach is idempotent");
#endif
        SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);Paint(edit,width,height);CheckSelectionAndClip(edit,width,height);
        CheckVerticalCenter(edit,width,height);
#ifndef DIRECT_WRITE_EDIT_BASELINE
        if(dpi==96){
            CheckMenu(edit);CheckMenu(edit,MenuAction::Deactivate);CheckMenu(edit,MenuAction::ReleaseCapture);
            const auto temporary=CreateWindowExW(0,L"EDIT",L"临时输入框",WS_CHILD|WS_VISIBLE,0,0,width,height,owner,nullptr,wc.hInstance,nullptr);
            Check(direct_write_edit::Attach(temporary),"attach editor for destruction during menu");
            CheckMenu(temporary,MenuAction::DestroyEditor);
        }
#endif
        SendMessageW(edit,EM_SETSEL,5,8);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"XYZ"));Check(Text(edit)==L"中文内容 XYZ 123","Chinese selection replacement");Check(SendMessageW(edit,EM_CANUNDO,0,0)!=0,"undo retained");SendMessageW(edit,WM_UNDO,0,0);Check(Text(edit)==L"中文内容 ABC 123","native undo");
        SendMessageW(edit,EM_SETSEL,0,-1);SendMessageW(edit,WM_CHAR,L'中',1);Check(Text(edit)==L"中","WM_CHAR replaces selection");
        SendMessageW(edit,WM_IME_STARTCOMPOSITION,0,0);SendMessageW(edit,WM_IME_COMPOSITION,0,0);SendMessageW(edit,WM_IME_COMPOSITION,0,GCS_RESULTSTR);SendMessageW(edit,WM_IME_ENDCOMPOSITION,0,0);Check(Text(edit)==L"中","IME lifecycle without actual result preserves text");
        SetWindowTextW(edit,L"A\U0001f600中B");SendMessageW(edit,EM_SETSEL,1,3);SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"汉"));Check(Text(edit)==L"A汉中B","UTF16 surrogate pair replacement");
        SetWindowTextW(edit,L"中ABC中文XYZ");SendMessageW(edit,EM_SETSEL,0,0);
        const auto position=SendMessageW(edit,EM_POSFROMCHAR,4,0);const auto hit=SendMessageW(edit,EM_CHARFROMPOS,0,position);Check(LOWORD(hit)==4,"DirectWrite position and hit test round trip");
        SendMessageW(edit,WM_LBUTTONDOWN,MK_LBUTTON,position);SendMessageW(edit,WM_LBUTTONUP,0,position);DWORD start=0,end=0;SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));Check(start==4&&end==4,"mouse selection uses DirectWrite positions");
        const auto dragStart=SendMessageW(edit,EM_POSFROMCHAR,1,0),dragEnd=SendMessageW(edit,EM_POSFROMCHAR,6,0);SendMessageW(edit,WM_LBUTTONDOWN,MK_LBUTTON,dragStart);SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,dragEnd);SendMessageW(edit,WM_LBUTTONUP,0,dragEnd);SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));Check(start==1&&end==6,"drag selection retains anchor");
        SetWindowTextW(edit,L"A\U0001f600 B");
        const auto emojiStart=SendMessageW(edit,EM_POSFROMCHAR,1,0),emojiEnd=SendMessageW(edit,EM_POSFROMCHAR,3,0);
        // Click inside the glyph: integer rounding of the leading boundary
        // can land in the preceding character at fractional DPI scales.
        const auto emoji=MAKELPARAM((GET_X_LPARAM(emojiStart)+GET_X_LPARAM(emojiEnd))/2,GET_Y_LPARAM(emojiStart)+2);
        SendMessageW(edit,WM_LBUTTONDBLCLK,MK_LBUTTON,emoji);SendMessageW(edit,WM_LBUTTONUP,0,emoji);
        SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
        Check(start==1&&end==3,"double click selects whole surrogate cluster");
        SetWindowTextW(edit,L"ab\u0301cd");
        for(const WPARAM character:{0u,4u}){
            const auto begin=SendMessageW(edit,EM_POSFROMCHAR,character,0),tail=SendMessageW(edit,EM_POSFROMCHAR,character+1,0);
            const auto point=MAKELPARAM((GET_X_LPARAM(begin)+GET_X_LPARAM(tail))/2,GET_Y_LPARAM(begin)+2);
            SendMessageW(edit,WM_LBUTTONDBLCLK,MK_LBUTTON,point);SendMessageW(edit,WM_LBUTTONUP,0,point);
            SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
            if(start!=0||end!=5)std::cerr<<"combining selection "<<dpi<<" char "<<character<<" range "<<start<<":"<<end<<'\n';
            Check(start==0&&end==5,"word expansion preserves adjacent combining clusters in both directions");
        }
        SetWindowTextW(edit,L"很长的输入内容 Very long input text 0123456789 0123456789 0123456789");SendMessageW(edit,EM_SETSEL,0x7fffffff,0x7fffffff);SendMessageW(edit,EM_SCROLLCARET,0,0);const auto last=SendMessageW(edit,EM_POSFROMCHAR,GetWindowTextLengthW(edit),0);Check(GET_X_LPARAM(last)>=0&&GET_X_LPARAM(last)<width,"horizontal scroll keeps caret visible");Paint(edit,width,height);
        DestroyWindow(edit);
        const auto multiline=CreateWindowExW(0,L"EDIT",L"第一行中文 abc\r\n第二行 emoji \U0001f600\r\n第三行\r\n第四行\r\n第五行",WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN,0,0,width,height*2,owner,nullptr,wc.hInstance,nullptr);
#ifndef DIRECT_WRITE_EDIT_BASELINE
        Check(direct_write_edit::Attach(multiline),"attach multiline");
#endif
        SendMessageW(multiline,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(multiline,EM_SETSEL,0x7fffffff,0x7fffffff);SendMessageW(multiline,EM_SCROLLCARET,0,0);const auto tail=SendMessageW(multiline,EM_POSFROMCHAR,GetWindowTextLengthW(multiline),0);Check(GET_Y_LPARAM(tail)>=0&&GET_Y_LPARAM(tail)<height*2,"vertical scroll keeps caret visible");Paint(multiline,width,height*2);SendMessageW(multiline,EM_LINESCROLL,0,-2);Check(SendMessageW(multiline,EM_GETFIRSTVISIBLELINE,0,0)>=0,"multiline scrolling available");CheckLines(multiline);DestroyWindow(multiline);
        const auto checkbox=CreateWindowExW(0,L"BUTTON",L"提醒",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,0,0,width,height,owner,nullptr,wc.hInstance,nullptr);
#ifndef DIRECT_WRITE_EDIT_BASELINE
        Check(direct_write_edit::Attach(checkbox),"attach native checkbox");
#endif
        SendMessageW(checkbox,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SendMessageW(checkbox,BM_CLICK,0,0);Check(SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_CHECKED,"checkbox native click still toggles");Dib checked(width,height);SendMessageW(checkbox,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(checked.dc),PRF_CLIENT);SendMessageW(checkbox,BM_CLICK,0,0);Check(SendMessageW(checkbox,BM_GETCHECK,0,0)==BST_UNCHECKED,"checkbox toggles off");Dib unchecked(width,height);SendMessageW(checkbox,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(unchecked.dc),PRF_CLIENT);GdiFlush();Check(!std::equal(checked.pixels,checked.pixels+width*height,unchecked.pixels),"checkbox check mark remains visible");DestroyWindow(checkbox);DeleteObject(font);
    }
    DestroyWindow(owner);if(failures)return 1;std::cout<<"PASS DirectWrite edit rendering/input/selection/undo/IME lifecycle/UTF16/scroll at four DPI scales\n";
}
