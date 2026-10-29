#include "platform/windows/screenshot/screenshot_editor.h"
#include "platform/windows/screenshot/screenshot_render.h"
#include <commctrl.h>
#include <array>
#include <cstdlib>
#include <iostream>
#include <chrono>
using namespace lume::platform::windows::capture;
void Check(bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';std::exit(1);}}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
void Drag(HWND hwnd,int x,int y,int endX,int endY){
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(endX,endY));SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(endX,endY));Pump();
}
void Invoke(HWND hwnd,Command c){SendMessageW(hwnd,WM_COMMAND,ScreenshotEditor::CommandFirst+static_cast<UINT>(c),0);Pump();}
RECT ButtonRect(HWND hwnd,int index){
    HWND tooltip=nullptr;
    struct Search{HWND owner;HWND* found;}search{hwnd,&tooltip};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM data)->BOOL{
        auto* target=reinterpret_cast<Search*>(data);wchar_t name[80]{};GetClassNameW(candidate,name,80);
        if(GetWindow(candidate,GW_OWNER)==target->owner&&wcscmp(name,TOOLTIPS_CLASSW)==0){*target->found=candidate;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    Check(tooltip!=nullptr,"toolbar has native tooltip owner");
    TOOLINFOW info{};info.cbSize=TTTOOLINFOW_V2_SIZE;info.hwnd=hwnd;info.uId=static_cast<UINT_PTR>(index+1);
    if(!SendMessageW(tooltip,TTM_GETTOOLINFOW,0,reinterpret_cast<LPARAM>(&info))){std::cerr<<"tooltip count="<<SendMessageW(tooltip,TTM_GETTOOLCOUNT,0,0)<<" index="<<index<<'\n';Check(false,"toolbar hit rectangle published");}return info.rect;
}
void ClickOption(HWND hwnd,int index){
    const auto r=ButtonRect(hwnd,99+index);Check(r.right>r.left,"tool option is visible");
    const auto p=MAKELPARAM((r.left+r.right)/2,(r.top+r.bottom)/2);
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,p);SendMessageW(hwnd,WM_LBUTTONUP,0,p);Pump();
}
Image Preview(HWND hwnd,int width,int height,bool children=false,bool presented=false){
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
    HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(dc&&bitmap&&bits,"preview DIB allocation");
    const auto old=SelectObject(dc,bitmap);
    if(presented){const HDC windowDc=GetDC(hwnd);Check(windowDc!=nullptr,"test-owned window DC available");
        Check(BitBlt(dc,0,0,width,height,windowDc,0,0,SRCCOPY)!=FALSE,"read presented test-owned window pixels");ReleaseDC(hwnd,windowDc);}
    else SendMessageW(hwnd,children?WM_PRINT:WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|(children?PRF_CHILDREN:0));GdiFlush();
    const auto pixels=static_cast<std::uint32_t*>(bits);Image result{width,height,std::vector<std::uint32_t>(pixels,pixels+static_cast<std::size_t>(width)*height)};
    for(auto& pixel:result.pixels)pixel|=0xff000000u;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return result;
}
void InlineTextInputRegression(){
    EditorPorts ports;
    ports.capture=[](Image& image,POINT& origin){
        image={800,600,std::vector<std::uint32_t>(480000)};origin={0,0};
        for(int y=0;y<600;++y)for(int x=0;x<800;++x)
            image.pixels[static_cast<std::size_t>(y)*800+x]=0xff000000u|
                (static_cast<unsigned>((x+15)&255)<<16)|(static_cast<unsigned>((y+35)&255)<<8)|0x72u;
        return true;
    };
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"inline text fixture starts");
    const HWND owner=editor.Handle();Drag(owner,100,100,500,380);Invoke(owner,Command::Text);
    SendMessageW(owner,WM_LBUTTONDOWN,0,MAKELPARAM(180,160));Pump();
    const HWND input=GetDlgItem(owner,4516);Check(input!=nullptr,"inline text input retains native edit control");
    RECT rect{};GetClientRect(input,&rect);
    const auto pixels=Preview(input,rect.right,rect.bottom);
    Check((GetWindowLongPtrW(input,GWL_EXSTYLE)&WS_EX_CLIENTEDGE)==0,"inline text input has no system border");
    for(const POINT point:std::array<POINT,4>{{{8,8},{rect.right-8,8},{8,rect.bottom-8},{rect.right-8,rect.bottom-8}}}){
        const auto actual=pixels.pixels[static_cast<std::size_t>(point.y)*rect.right+point.x]&0x00ffffffu;
        const auto expected=(static_cast<unsigned>((180+point.x+15)&255)<<16)|
            (static_cast<unsigned>((160+point.y+35)&255)<<8)|0x72u;
        Check(actual==expected,"inline text background follows the screenshot pixels without repeating");
    }
    SendMessageW(input,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"直接在截图上输入文字"));Pump();
    RECT resized{};GetClientRect(input,&resized);
    const auto typed=Preview(input,resized.right,resized.bottom);
    const POINT clear{resized.right-5,resized.bottom-5};
    const auto typedPixel=typed.pixels[static_cast<std::size_t>(clear.y)*resized.right+clear.x]&0x00ffffffu;
    const auto expectedTyped=(static_cast<unsigned>((180+clear.x+15)&255)<<16)|
        (static_cast<unsigned>((160+clear.y+35)&255)<<8)|0x72u;
    if(typedPixel!=expectedTyped)std::cerr<<"resized input="<<std::hex<<typedPixel<<" expected="<<expectedTyped<<std::dec<<'\n';
    Check(typedPixel==expectedTyped,"resized text input still matches the screenshot background");
    const auto parentPrint=Preview(owner,800,600,true);
    Check((parentPrint.pixels[static_cast<std::size_t>(160+clear.y)*800+180+clear.x]&0x00ffffffu)==expectedTyped,
        "text background also aligns when its parent prints the child");
    if(const auto previewPath=_wgetenv(L"LUME_SCREENSHOT_TEXT_PREVIEW")){
        auto proof=Preview(owner,800,600);
        for(int y=0;y<resized.bottom;++y)for(int x=0;x<resized.right;++x)
            proof.pixels[static_cast<std::size_t>(160+y)*800+180+x]=typed.pixels[static_cast<std::size_t>(y)*resized.right+x];
        Check(WritePng(proof,previewPath),"write inline text input visual proof");
    }
    SendMessageW(input,EM_SETSEL,0,-1);
    SendMessageW(input,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(
        L"一段较长的文字需要在截图上直接换行而不是被白色输入框裁切。继续输入时应该能看见所有行，编辑完成后还要保持相同的排版。"));
    Pump();
    RECT multiline{};GetClientRect(input,&multiline);
    const auto lineCount=static_cast<int>(SendMessageW(input,EM_GETLINECOUNT,0,0));
    Check(lineCount>=2 && multiline.bottom>=std::min(180,lineCount*24),
        "inline text input grows for wrapped lines");
    editor.Stop();
}
void ToolbarPresentationRegression(const std::wstring& previewPath=L""){
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){image={800,600,std::vector<std::uint32_t>(480000,0xff6688aa)};origin={0,0};return true;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"toolbar presentation fixture starts");const HWND hwnd=editor.Handle();
    for(unsigned dpi:{96u,120u,144u,192u,240u})for(int bottom:{180,390,520}){
    SendMessageW(hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),0);
    // Clear the previous selection before creating a new one.
    Drag(hwnd,10,10,10,10);Drag(hwnd,120,50,700,bottom);UpdateWindow(hwnd);
    for(int stage=0;stage<3;++stage){
        if(stage==1){const auto button=ButtonRect(hwnd,2);SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM((button.left+button.right)/2,(button.top+button.bottom)/2));UpdateWindow(hwnd);}
        if(stage==2){SendMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(10,10));UpdateWindow(hwnd);}
        const auto actual=Preview(hwnd,800,600,false,true),expected=Preview(hwnd,800,600);
        for(int i=0;i<CommandCount;++i){const auto r=ButtonRect(hwnd,i);int mismatch=0;
            for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x){const auto p=static_cast<std::size_t>(y)*800+x;if(actual.pixels[p]!=expected.pixels[p])++mismatch;}
            if(mismatch){std::cerr<<"toolbar stage="<<stage<<" button="<<i<<" mismatched pixels="<<mismatch<<'\n';
                wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);WritePng(actual,std::wstring(temp)+L"lume-toolbar-presentation-failure.png");}
            Check(mismatch==0,"presented toolbar icons match full render after partial repaint");
        }
    }
    }
    if(!previewPath.empty())Check(WritePng(Preview(hwnd,800,600,false,true),previewPath),"save actual toolbar presentation preview");
    editor.Stop();
}
void PreviewRegression(){
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){image={800,600,std::vector<std::uint32_t>(480000)};origin={0,0};
        for(int y=0;y<600;++y)for(int x=0;x<800;++x)image.pixels[static_cast<std::size_t>(y)*800+x]=0xff000000u|static_cast<unsigned>(((x*17+y)&255)<<16)|static_cast<unsigned>(((x+y*7)&255)<<8)|static_cast<unsigned>((x*3+y)&255);
        return true;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"preview fixture starts");const HWND hwnd=editor.Handle();
    Drag(hwnd,50,50,750,400);
    for(auto tool:{Command::Shape,Command::Arrow,Command::Pen,Command::Marker,Command::Mosaic,Command::Eraser}){
        Invoke(hwnd,tool);
        const int variants=tool==Command::Mosaic?4:1;
        for(int v=0;v<variants;++v){
            if(tool==Command::Mosaic)ClickOption(hwnd,1+v);
            SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(180,150));
            SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(600,330));UpdateWindow(hwnd);
            SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(260,230));UpdateWindow(hwnd);
            const auto presented=Preview(hwnd,800,600,false,true),first=Preview(hwnd,800,600),repeat=Preview(hwnd,800,600);
            for(int y=100;y<350;++y)for(int x=100;x<650;++x){const auto pixel=static_cast<std::size_t>(y)*800+x;
                Check(presented.pixels[pixel]==first.pixels[pixel],"actual partial stroke presentation matches full render");}
            Check(first.pixels==repeat.pixels,"repeated paint never accumulates marker opacity or filter strength");
            SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(260,230));Pump();const auto output=editor.Result();
            Check(output.Valid(),"preview fixture commits output");
            for(int y=10;y<output.height-10;++y)for(int x=10;x<output.width-10;++x){
                Check(first.pixels[static_cast<std::size_t>(y+50)*800+x+50]==output.pixels[static_cast<std::size_t>(y)*output.width+x],"live preview pixels match committed output without stale stroke tails");
            }
            Invoke(hwnd,Command::Undo);
        }
    }
    editor.Stop();
}
void LayoutMatrix(){
    for(unsigned dpi:{96u,120u,144u,192u})for(int width:{320,800,1440}){
        const int height=width==320?480:900;
        EditorPorts ports;ports.capture=[=](Image& image,POINT& origin){image={width,height,std::vector<std::uint32_t>(static_cast<std::size_t>(width)*height,0xff6688aa)};origin={0,0};return true;};
        ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"layout fixture starts");
        const HWND hwnd=editor.Handle();SendMessageW(hwnd,WM_DPICHANGED,MAKELONG(dpi,dpi),0);
        Drag(hwnd,20,20,width-20,height-60);Invoke(hwnd,Command::Shape);
        RECT previous{};
        for(int i=0;i<CommandCount;++i){
            const auto r=ButtonRect(hwnd,i);
            Check(r.left>=0&&r.top>=0&&r.right<=width&&r.bottom<=height&&r.right>r.left&&r.bottom>r.top,"DPI toolbar button inside physical image");
            if(i){RECT overlap{};Check(!IntersectRect(&overlap,&r,&previous),"adjacent toolbar commands do not overlap");}
            previous=r;
        }
        for(int i=0;i<14;++i){const auto r=ButtonRect(hwnd,99+i);Check(r.left>=0&&r.top>=0&&r.right<=width&&r.bottom<=height&&r.right>r.left&&r.bottom>r.top,"DPI tool options inside physical image");}
        for(auto tool:{Command::Text,Command::Mosaic,Command::Arrow,Command::Pen}){
            Invoke(hwnd,tool);
            for(int i=0;i<64;++i){const auto r=ButtonRect(hwnd,99+i);if(r.right<=r.left)continue;
                Check(r.left>=0&&r.top>=0&&r.right<=width&&r.bottom<=height,"all parameter palette cells fit narrow and scaled views");}
            for(int id:{4510,4511,4512,4515}){const HWND control=GetDlgItem(hwnd,id);if(!IsWindowVisible(control))continue;
                RECT r{};GetWindowRect(control,&r);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);
                Check(r.left>=0&&r.top>=0&&r.right<=width&&r.bottom<=height,"native option controls fit narrow and scaled views");}
        }
        editor.Stop();
    }
}
void ReferenceOptionsRegression(const std::wstring& previewDirectory=L""){
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){image={1000,700,std::vector<std::uint32_t>(700000,0xff6688aa)};origin={0,0};return true;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"reference options fixture starts");const HWND hwnd=editor.Handle();
    Drag(hwnd,80,80,850,450);Invoke(hwnd,Command::Text);
    const auto capture=[&](const wchar_t* name){if(!previewDirectory.empty())Check(WritePng(Preview(hwnd,1000,700,true),previewDirectory+L"/"+name+L".png"),"reference options preview saved");};
    const auto choose=[&](int id,int value){const HWND control=GetDlgItem(hwnd,id);Check(control!=nullptr,"option control exists");
        Check(SendMessageW(control,CB_SETCURSEL,value,0)!=CB_ERR,"option selection exists");SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control));Pump();};
    const auto slider=[&](int id,int value){const HWND control=GetDlgItem(hwnd,id);SendMessageW(control,TBM_SETPOS,TRUE,value);SendMessageW(hwnd,WM_HSCROLL,TB_THUMBPOSITION,reinterpret_cast<LPARAM>(control));Pump();};
    const auto enterText=[&](){SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(180,160));const HWND input=GetDlgItem(hwnd,4516);
        Check(input!=nullptr,"styled text input opens");SetWindowTextW(input,L"Lume \u622a\u56fe");SendMessageW(input,WM_KEYDOWN,VK_RETURN,0);Pump();return editor.Result();};
    Check(GetDlgItem(hwnd,4510)!=nullptr,"text toolbar exposes actual font selector");
    Check(GetDlgItem(hwnd,4511)!=nullptr,"text toolbar exposes actual font size selector");
    capture(L"text");
    const auto plain=enterText();Invoke(hwnd,Command::Undo);
    choose(4510,5);choose(4511,10);ClickOption(hwnd,0);ClickOption(hwnd,1);ClickOption(hwnd,2);
    const auto styled=enterText();Check(styled.Valid()&&styled.pixels!=plain.pixels,"font family size bold italic outline change committed pixels");
    Invoke(hwnd,Command::Undo);Invoke(hwnd,Command::Redo);Check(editor.Result().pixels==styled.pixels,"styled text survives undo redo");Invoke(hwnd,Command::Undo);
    Invoke(hwnd,Command::Arrow);std::vector<std::vector<std::uint32_t>> arrows;
    for(int v=0;v<4;++v){choose(4515,v);Drag(hwnd,180,160,420,240);auto result=editor.Result();
        for(const auto& pixels:arrows)Check(result.pixels!=pixels,"arrow choices produce distinct output");arrows.push_back(std::move(result.pixels));Invoke(hwnd,Command::Undo);}
    capture(L"arrow");
    Invoke(hwnd,Command::Pen);Drag(hwnd,180,160,420,240);const auto thin=editor.Result();Invoke(hwnd,Command::Undo);
    ClickOption(hwnd,0);slider(4513,12);capture(L"pen-size");ClickOption(hwnd,0);Drag(hwnd,180,160,420,240);
    Check(editor.Result().pixels!=thin.pixels,"stroke slider changes drawn thickness");Invoke(hwnd,Command::Undo);
    Invoke(hwnd,Command::Mosaic);Check(IsWindowVisible(GetDlgItem(hwnd,4512)),"mosaic strength is independent slider");
    ClickOption(hwnd,0);slider(4513,65);ClickOption(hwnd,0);slider(4512,9);capture(L"mosaic");
    ClickOption(hwnd,0);Check(SendMessageW(GetDlgItem(hwnd,4513),TBM_GETPOS,0,0)==65,"strength leaves brush size unchanged");ClickOption(hwnd,0);
    Invoke(hwnd,Command::Pen);ClickOption(hwnd,0);
    Check(SendMessageW(GetDlgItem(hwnd,4513),TBM_GETPOS,0,0)==12,"mosaic strength does not change pen thickness");
    slider(4513,100);Invoke(hwnd,Command::Mosaic);
    Check(SendMessageW(GetDlgItem(hwnd,4512),TBM_GETPOS,0,0)==9,"pen thickness does not change mosaic strength");
    Drag(hwnd,900,600,900,600);
    Check(!IsWindowVisible(GetDlgItem(hwnd,4512)),"clearing selection hides native mosaic controls");
    Drag(hwnd,80,80,850,450);Invoke(hwnd,Command::Text);Drag(hwnd,900,600,900,600);
    Check(!IsWindowVisible(GetDlgItem(hwnd,4510))&&!IsWindowVisible(GetDlgItem(hwnd,4511)),"clearing selection hides native text controls");
    editor.Stop();
}
void LimitRegression(){
    int copies=0;EditorPorts ports;
    ports.capture=[](Image& image,POINT& origin){image={640,480,std::vector<std::uint32_t>(307200,0xffeeeeee)};origin={0,0};return true;};
    ports.copy=[&](HWND,const Image&){++copies;return false;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"limit fixture starts");
    const HWND hwnd=editor.Handle();Drag(hwnd,20,20,620,330);Invoke(hwnd,Command::Shape);
    for(int i=0;i<256;++i){
        const int x=70+i%16,y=70+i%8;const auto start=MAKELPARAM(x,y),end=MAKELPARAM(x+20,y+15);
        SendMessageW(hwnd,WM_LBUTTONDOWN,0,start);SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,end);SendMessageW(hwnd,WM_LBUTTONUP,0,end);
    }
    Check(editor.Result().Valid(),"maximum accepted annotation count remains exportable");
    Invoke(hwnd,Command::Mosaic);Drag(hwnd,60,60,180,140);
    Check(!editor.Result().Valid(),"rejected redaction blocks output instead of exporting stale pixels");
    Invoke(hwnd,Command::Copy);Check(copies==0&&editor.Active(),"annotation limit never copies missing redaction");
    Invoke(hwnd,Command::Undo);Drag(hwnd,60,60,180,140);
    Check(editor.Result().Valid(),"undo creates room for retrying rejected annotation");editor.Stop();
}
void DirtyPaintRegression(){
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){image={1920,1080,std::vector<std::uint32_t>(2073600,0xff6688aa)};origin={0,0};return true;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"dirty paint fixture starts");
    const HWND hwnd=editor.Handle();Drag(hwnd,80,80,1500,750);Invoke(hwnd,Command::Pen);UpdateWindow(hwnd);
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(200,200));
    SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(230,220));
    RECT dirty{};Check(GetUpdateRect(hwnd,&dirty,FALSE),"live stroke requests a paint");
    Check((dirty.right-dirty.left)*(dirty.bottom-dirty.top)<1920*1080/4,"live stroke invalidates its local bounds, not the whole desktop");
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(230,220));editor.Stop();
}
void StrokeLimitRegression(){
    int copies=0;EditorPorts ports;
    ports.capture=[](Image& image,POINT& origin){image={640,480,std::vector<std::uint32_t>(307200,0xff6688aa)};origin={0,0};return true;};
    ports.copy=[&](HWND,const Image&){++copies;return false;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);Check(editor.Start(),"stroke limit fixture starts");
    const HWND hwnd=editor.Handle();Drag(hwnd,20,20,620,330);Invoke(hwnd,Command::Mosaic);
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(100,100));
    for(int i=0;i<20001;++i)SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(100+i%2,100));
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(400,200));
    Check(!editor.Result().Valid(),"point limit rejects incomplete redaction instead of exporting truncated stroke");
    Invoke(hwnd,Command::Copy);Check(copies==0&&editor.Active(),"point limit prevents incomplete redaction copy");
    Invoke(hwnd,Command::Undo);Drag(hwnd,100,100,180,140);
    Check(editor.Result().Valid(),"undo allows retry after overlong stroke");editor.Stop();
}
void ControlKeyboardRegression(){
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){image={800,600,std::vector<std::uint32_t>(480000,0xff6688aa)};origin={0,0};return true;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);
    for(int id:{4512,4513,4515,4510}){
        Check(editor.Start(),"keyboard controls fixture starts");const HWND hwnd=editor.Handle();Drag(hwnd,20,20,700,400);
        Invoke(hwnd,id==4510?Command::Text:id==4515?Command::Arrow:Command::Mosaic);
        if(id==4513)ClickOption(hwnd,0);
        const HWND control=GetDlgItem(hwnd,id);SetFocus(control);Check(GetFocus()==control,"real native control has focus");
        if(id==4515||id==4510){
            SendMessageW(control,CB_SHOWDROPDOWN,TRUE,0);SendMessageW(GetFocus(),WM_KEYDOWN,VK_DOWN,0);Pump();
            Check(GetFocus()==control&&SendMessageW(control,CB_GETDROPPEDSTATE,0,0),"keyboard navigation keeps dropdown open and focused");
            SendMessageW(GetFocus(),WM_KEYDOWN,VK_ESCAPE,0);Pump();
            Check(editor.Active()&&!SendMessageW(control,CB_GETDROPPEDSTATE,0,0),"Escape closes dropdown before editor");
        }
        SendMessageW(GetFocus(),WM_KEYDOWN,VK_ESCAPE,0);Pump();
        Check(!editor.Active(),"Escape from focused native option closes screenshot editor");
    }
    Check(editor.Start(),"canvas focus fixture starts");const HWND hwnd=editor.Handle();Drag(hwnd,20,20,700,400);Invoke(hwnd,Command::Mosaic);
    SetFocus(GetDlgItem(hwnd,4512));SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(120,120));
    Check(GetFocus()==hwnd,"drawing on canvas restores editor keyboard focus");SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(150,150));editor.Stop();
}
void PerformanceProbe(){
    using Clock=std::chrono::steady_clock;
    EditorPorts ports;ports.capture=[](Image& image,POINT& origin){
        image={3840,2160,std::vector<std::uint32_t>(3840u*2160u)};origin={0,0};
        for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x)image.pixels[static_cast<std::size_t>(y)*image.width+x]=0xff000000u|static_cast<unsigned>(((x*11+y*17)&255)<<16)|static_cast<unsigned>(((x+y*3)&255)<<8)|static_cast<unsigned>((x*5+y)&255);
        return true;
    };
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);
    const auto start=Clock::now();Check(editor.Start(),"performance fixture starts");Pump();
    std::cout<<"BENCH start_4k_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-start).count()<<'\n';
    const HWND hwnd=editor.Handle();
    const auto measure=[&](const char* name,int x,int y,int count){
        std::vector<double> times;
        SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(x,y));UpdateWindow(hwnd);
        for(int i=0;i<count;++i){const auto begin=Clock::now();
            SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+20+i*3,y+20+(i%12)*3));UpdateWindow(hwnd);
            times.push_back(std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
        }
        SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x+20+(count-1)*3,y+20+((count-1)%12)*3));Pump();
        std::sort(times.begin(),times.end());
        std::cout<<"BENCH "<<name<<" median_ms="<<times[times.size()/2]<<" p95_ms="<<times[times.size()*95/100]<<'\n';
    };
    measure("selection_4k",80,80,40);
    Drag(hwnd,20,20,1700,950);Invoke(hwnd,Command::Shape);
    for(int i=0;i<20;++i)Drag(hwnd,100+i*12,100+i*8,350+i*12,350+i*8);
    Invoke(hwnd,Command::Pen);measure("pen_4k_20_annotations",500,250,80);
    Invoke(hwnd,Command::Mosaic);measure("mosaic_4k",600,450,80);
    editor.Stop();
}
int main(int argc,char** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc>1&&std::string(argv[1])=="--benchmark"){PerformanceProbe();return 0;}
    if(argc>1&&std::string(argv[1])=="--dirty-regression"){DirtyPaintRegression();return 0;}
    if(argc>1&&std::string(argv[1])=="--options-regression"){ReferenceOptionsRegression();return 0;}
    if(argc>2&&std::string(argv[1])=="--reference-preview"){const std::string path=argv[2];ReferenceOptionsRegression(std::wstring(path.begin(),path.end()));return 0;}
    if(argc>1&&std::string(argv[1])=="--stroke-limit-regression"){StrokeLimitRegression();return 0;}
    if(argc>1&&std::string(argv[1])=="--keyboard-regression"){ControlKeyboardRegression();return 0;}
    if(argc>1&&std::string(argv[1])=="--toolbar-regression"){const std::string path=argc>2?argv[2]:"";ToolbarPresentationRegression(std::wstring(path.begin(),path.end()));return 0;}
    int captured=0,copied=0,saved=0;bool accept=false;Image exported;
    EditorPorts ports;
    ports.capture=[&](Image& image,POINT& origin){++captured;image={800,600,std::vector<std::uint32_t>(480000,0xffc0d6de)};origin={40,40};return true;};
    ports.copy=[&](HWND,const Image& image){++copied;exported=image;return accept;};
    ports.save=[&](HWND,const Image& image){++saved;exported=image;SetLastError(accept?ERROR_SUCCESS:ERROR_CANCELLED);return accept;};
    ScreenshotEditor editor(GetModuleHandleW(nullptr),ports);
    Check(editor.Start()&&editor.Active(),"editor starts from injected nonprivate image");
    Check(editor.Start()&&captured==1,"reentry does not recapture");
    auto hwnd=editor.Handle();Drag(hwnd,100,100,500,360);
    Check(editor.Result().width==400&&editor.Result().height==260,"selection crop physical dimensions");
    const auto baseline=editor.Result();
    const auto shapeButton=ButtonRect(hwnd,0);
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM((shapeButton.left+shapeButton.right)/2,(shapeButton.top+shapeButton.bottom)/2));
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM((shapeButton.left+shapeButton.right)/2,(shapeButton.top+shapeButton.bottom)/2));
    Drag(hwnd,150,140,280,240);
    Check(editor.Result().pixels!=baseline.pixels,"native shape gesture edits output");
    Invoke(hwnd,Command::Undo);Check(editor.Result().pixels==baseline.pixels,"native undo restores original");
    Invoke(hwnd,Command::Redo);Check(editor.Result().pixels!=baseline.pixels,"native redo restores annotation");
    const auto annotated=editor.Result();
    Invoke(hwnd,Command::Eraser);Drag(hwnd,150,160,150,180);
    const auto partiallyErased=editor.Result();
    Check(partiallyErased.pixels!=baseline.pixels&&partiallyErased.pixels!=annotated.pixels,"eraser removes only pixels along the stroke, preserving the rest of the shape");
    Invoke(hwnd,Command::Undo);Check(editor.Result().pixels==annotated.pixels,"one eraser drag is one undoable pixel operation");
    Invoke(hwnd,Command::Redo);Check(editor.Result().pixels==partiallyErased.pixels,"redo restores the partial pixel erasure");
    Invoke(hwnd,Command::Shape);
    for(int variant=0;variant<4;++variant){
        const auto before=editor.Result();ClickOption(hwnd,1+variant);Drag(hwnd,320,140,430,210);
        Check(editor.Result().pixels!=before.pixels,"shape option produces rendered result");Invoke(hwnd,Command::Undo);
    }
    for(auto tool:{Command::Arrow,Command::Pen,Command::Marker}){const auto before=editor.Result();Invoke(hwnd,tool);Drag(hwnd,180,200,400,270);Check(editor.Result().pixels!=before.pixels,"native drawing tool gesture changes crop");}
    Invoke(hwnd,Command::Mosaic);
    SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(130,125));
    SendMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(420,290));
    Invoke(hwnd,Command::Copy);Invoke(hwnd,Command::Save);Invoke(hwnd,Command::Pin);
    Check(copied==0&&saved==0&&editor.Active(),"in-progress redaction cannot export stale unredacted pixels");
    Check(!editor.Result().Valid(),"in-progress crop is unavailable to output callers");
    SendMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(420,290));Invoke(hwnd,Command::Undo);
    for(int variant=0;variant<4;++variant){
        const auto before=editor.Result();ClickOption(hwnd,1+variant);Drag(hwnd,130,125,420,290);
        Check(editor.Result().pixels!=before.pixels,"mosaic and blur options edit actual composed pixels");Invoke(hwnd,Command::Undo);
    }
    Invoke(hwnd,Command::Text);SendMessageW(hwnd,WM_LBUTTONDOWN,0,MAKELPARAM(180,160));Pump();
    HWND text=GetDlgItem(hwnd,4516);Check(text!=nullptr,"native Unicode text editor opens");
    SetWindowTextW(text,L"Lume \u622a\u56fe");SendMessageW(text,WM_KEYDOWN,VK_RETURN,0);Pump();
    Check(!IsWindow(text),"text commits and removes input child");
    if(argc>1){
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=800;info.bmiHeader.biHeight=-600;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* bits=nullptr;
        HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);Check(dc&&bitmap&&bits,"fixture preview surface");
        const auto old=SelectObject(dc,bitmap);SendMessageW(hwnd,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        const auto pixels=static_cast<std::uint32_t*>(bits);Image preview{800,600,std::vector<std::uint32_t>(pixels,pixels+480000)};
        for(auto& pixel:preview.pixels)pixel|=0xff000000u;
        const std::string path=argv[1];Check(WritePng(preview,std::wstring(path.begin(),path.end())),"write synthetic preview PNG");
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    }
    Invoke(hwnd,Command::Copy);Check(copied==1&&editor.Active()&&exported.Valid(),"copy failure retains editor and image");
    Invoke(hwnd,Command::Save);Check(saved==1&&editor.Active(),"save cancellation retains editor");
    accept=true;Invoke(hwnd,Command::Copy);Check(!editor.Active()&&!editor.Handle()&&copied==2,"successful copy closes and releases overlay");
    Check(editor.Start(),"editor restarts after copy");hwnd=editor.Handle();Drag(hwnd,80,80,300,220);
    Invoke(hwnd,Command::Pin);Check(!editor.Active(),"pin closes editor");
    HWND pin=FindWindowW(L"LumeScreenshotPin",nullptr);Check(pin!=nullptr,"pin creates independent native image window");
    SendMessageW(pin,WM_CLOSE,0,0);Pump();Check(!IsWindow(pin),"pin can close independently");
    Check(editor.Start(),"editor restarts after pin");SendMessageW(editor.Handle(),WM_KEYDOWN,VK_ESCAPE,0);Check(!editor.Active(),"escape cancels");
    Check(editor.Start(),"editor starts before topology change");SendMessageW(editor.Handle(),WM_DISPLAYCHANGE,0,0);Check(!editor.Active(),"display changes cancel stale screenshot");
    editor.Stop();InlineTextInputRegression();LayoutMatrix();LimitRegression();StrokeLimitRegression();ControlKeyboardRegression();DirtyPaintRegression();ToolbarPresentationRegression();PreviewRegression();ReferenceOptionsRegression();std::cout<<"PASS native screenshot gestures, text, output failure retention, pin, DPI layout, redaction limits and lifecycle\n";
}
