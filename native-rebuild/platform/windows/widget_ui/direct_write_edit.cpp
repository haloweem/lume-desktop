#include "direct_write_edit.h"
#include "LumeWidgetMenu.h"
#include <commctrl.h>
#include <d2d1.h>
#include <dwrite.h>
#include <imm.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

namespace lume::platform::windows::direct_write_edit {
namespace {
constexpr wchar_t kProperty[] = L"Lume.DirectWriteEdit.State";
enum class Kind { Edit, Label, Button };
template<class T> struct Com {
    T* value=nullptr;
    ~Com(){if(value)value->Release();}
    T* operator->() const{return value;}
    T** Out(){if(value){value->Release();value=nullptr;}return &value;}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
struct State {
    HWND hwnd=nullptr;
    WNDPROC previous=nullptr;
    Kind kind=Kind::Edit;
    Com<IDWriteFactory> write;
    Com<ID2D1Factory> draw;
    Com<ID2D1DCRenderTarget> target;
    bool multiline=false,redraw=true,dragging=false,composing=false;
    bool syncing=false,alive=true;
    unsigned references=1;
    UINT32 anchor=0,active=0,previousStart=0,previousEnd=0;
    UINT32 compositionStart=0,compositionCursor=0;
    std::wstring composition;
    std::wstring cue;
    bool cueWhenFocused=false;
    float scrollX=0,scrollY=0,lineHeight=16;
    float caretHeight=0;
    ~State()=default;
    LRESULT Native(UINT message,WPARAM w=0,LPARAM l=0) const {
        return CallWindowProcW(previous,hwnd,message,w,l);
    }
    void Release(){if(!--references)delete this;}
};
// EN_CHANGE can synchronously destroy the control. Each procedure invocation
// keeps its state alive until all calls into the original EDIT have returned.
struct Reference {
    State* state;
    explicit Reference(State* s):state(s){++state->references;}
    ~Reference(){state->Release();}
};
std::wstring Text(State& s){
    const int count=static_cast<int>(s.Native(WM_GETTEXTLENGTH));
    std::wstring text(static_cast<size_t>(count)+1,L'\0');
    text.resize(static_cast<size_t>(s.Native(WM_GETTEXT,text.size(),reinterpret_cast<LPARAM>(text.data()))));
    return text;
}
struct Frame {
    Com<IDWriteTextFormat> format;
    Com<IDWriteTextLayout> layout;
    std::wstring text;
    RECT rect{};
    UINT32 nativeLength=0,compositionLength=0;
    float width=0,height=0;
    bool placeholder=false;
    bool Build(State& s){
        GetClientRect(s.hwnd,&rect);
        const auto style=GetWindowLongPtrW(s.hwnd,GWL_STYLE);
        if(s.kind==Kind::Edit){
            RECT edit{};s.Native(EM_GETRECT,0,reinterpret_cast<LPARAM>(&edit));
            if(edit.right>edit.left&&edit.bottom>edit.top){
                // Native single-line EDIT's formatting rectangle is only one
                // GDI line tall at the top. Keep its horizontal margins, but
                // center DirectWrite within the actual control height.
                rect.left=edit.left;rect.right=edit.right;
                if(s.multiline){rect.top=edit.top;rect.bottom=edit.bottom;}
            }
        }
        text=Text(s);nativeLength=static_cast<UINT32>(text.size());
        if(s.kind==Kind::Edit&&text.empty()&&!s.composing&&!s.cue.empty()&&
            (s.cueWhenFocused||GetFocus()!=s.hwnd)){
            text=s.cue;placeholder=true;
        }
        if(s.kind==Kind::Button){
            const auto type=style&BS_TYPEMASK;
            if(type==BS_AUTOCHECKBOX||type==BS_CHECKBOX){
                const auto controlHeight=rect.bottom-rect.top;
                rect.left+=std::min<LONG>(controlHeight,20)+4;
            }
        }
        if(s.kind==Kind::Edit&&s.composing&&!s.composition.empty()){
            const auto start=std::min(s.compositionStart,nativeLength);
            // Some Windows input services expose the in-progress text through
            // EDIT, while others keep it only in the HIMC until commit.
            if(text.compare(start,s.composition.size(),s.composition)!=0){
                text.insert(start,s.composition);compositionLength=static_cast<UINT32>(s.composition.size());
            }
        }
        if(s.kind==Kind::Edit&&(style&ES_PASSWORD)){
            const auto mask=static_cast<wchar_t>(s.Native(EM_GETPASSWORDCHAR));
            std::fill(text.begin(),text.end(),mask?mask:L'\x25cf');
        }
        LOGFONTW font{};
        const auto handle=reinterpret_cast<HFONT>(s.Native(WM_GETFONT));
        if(handle)GetObjectW(handle,sizeof(font),&font);
        if(!font.lfHeight)font.lfHeight=-13;
        if(!font.lfFaceName[0])wcscpy_s(font.lfFaceName,L"Microsoft YaHei UI");
        if(!font.lfWeight)font.lfWeight=FW_NORMAL;
        if(FAILED(s.write->CreateTextFormat(font.lfFaceName,nullptr,
            static_cast<DWRITE_FONT_WEIGHT>(std::clamp<LONG>(font.lfWeight,100,900)),
            font.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,static_cast<float>(std::abs(font.lfHeight)),L"",format.Out())))return false;
        const bool wrap=s.multiline&&!(style&ES_AUTOHSCROLL);
        format->SetWordWrapping(wrap?DWRITE_WORD_WRAPPING_WRAP:DWRITE_WORD_WRAPPING_NO_WRAP);
        auto align=DWRITE_TEXT_ALIGNMENT_LEADING;
        if(s.kind==Kind::Edit){
            if(style&ES_CENTER)align=DWRITE_TEXT_ALIGNMENT_CENTER;
            else if(style&ES_RIGHT)align=DWRITE_TEXT_ALIGNMENT_TRAILING;
        }else if(s.kind==Kind::Button){
            const auto type=style&BS_TYPEMASK;
            align=type==BS_AUTOCHECKBOX||type==BS_CHECKBOX?DWRITE_TEXT_ALIGNMENT_LEADING:DWRITE_TEXT_ALIGNMENT_CENTER;
        }
        else if((style&SS_TYPEMASK)==SS_CENTER)align=DWRITE_TEXT_ALIGNMENT_CENTER;
        else if((style&SS_TYPEMASK)==SS_RIGHT)align=DWRITE_TEXT_ALIGNMENT_TRAILING;
        format->SetTextAlignment(align);
        format->SetParagraphAlignment(s.multiline||s.kind==Kind::Label?
            DWRITE_PARAGRAPH_ALIGNMENT_NEAR:DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        width=static_cast<float>(std::max<LONG>(1,rect.right-rect.left));
        height=static_cast<float>(std::max<LONG>(1,rect.bottom-rect.top));
        if(FAILED(s.write->CreateTextLayout(text.data(),static_cast<UINT32>(text.size()),format.value,
            width,s.multiline?10000000.f:height,layout.Out())))return false;
        if(s.composing&&!s.composition.empty())layout->SetUnderline(TRUE,
            {std::min(s.compositionStart,static_cast<UINT32>(text.size())),static_cast<UINT32>(s.composition.size())});
        return true;
    }
    UINT32 DisplayPosition(const State& s,UINT32 position) const {
        if(s.composing&&position>=s.compositionStart){
            if(position==s.compositionStart)return position+std::min(s.compositionCursor,static_cast<UINT32>(s.composition.size()));
            return position+compositionLength;
        }
        return position;
    }
    UINT32 NativePosition(const State& s,UINT32 position) const {
        if(compositionLength&&position>s.compositionStart)
            position-=std::min(position-s.compositionStart,compositionLength);
        return std::min(position,nativeLength);
    }
    bool Point(UINT32 position,float& x,float& y,DWRITE_HIT_TEST_METRICS& metrics) const {
        return SUCCEEDED(layout->HitTestTextPosition(std::min(position,static_cast<UINT32>(text.size())),FALSE,&x,&y,&metrics));
    }
    UINT32 Hit(const State& s,float x,float y) const {
        BOOL trailing=FALSE,inside=FALSE;DWRITE_HIT_TEST_METRICS metrics{};
        if(FAILED(layout->HitTestPoint(x-rect.left+s.scrollX,y-rect.top+s.scrollY,&trailing,&inside,&metrics)))return 0;
        return NativePosition(s,metrics.textPosition+(trailing?metrics.length:0));
    }
};
void Selection(State& s){
    DWORD start=0,end=0;s.Native(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    if(start==end)s.anchor=s.active=end;
    else if(start!=s.previousStart&&end==s.previousEnd){s.active=start;s.anchor=end;}
    else if(end!=s.previousEnd){s.active=end;s.anchor=start;}
    s.previousStart=start;s.previousEnd=end;
}
void SetSelection(State& s,UINT32 anchor,UINT32 active){
    s.Native(EM_SETSEL,anchor,active);s.anchor=anchor;s.active=active;
    s.previousStart=std::min(anchor,active);s.previousEnd=std::max(anchor,active);
}
void Sync(State& s,bool reveal){
    if(!s.alive||s.kind!=Kind::Edit||s.syncing)return;
    s.syncing=true;
    Frame frame;
    if(frame.Build(s)){
        float x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
        if(frame.Point(frame.DisplayPosition(s,s.active),x,y,metrics)){
            s.lineHeight=std::max(1.f,metrics.height);
            if(reveal){
                if(x<s.scrollX)s.scrollX=x;
                else if(x-s.scrollX>=frame.width-1)s.scrollX=x-frame.width+2;
                if(s.multiline){
                    if(y<s.scrollY)s.scrollY=y;
                    else if(y+metrics.height-s.scrollY>frame.height)s.scrollY=y+metrics.height-frame.height;
                }
            }
            DWRITE_TEXT_METRICS textMetrics{};frame.layout->GetMetrics(&textMetrics);
            // Center/right-aligned overflowing text can have a negative left.
            s.scrollX=std::clamp(s.scrollX,std::min(0.f,textMetrics.left),
                std::max(0.f,textMetrics.left+textMetrics.widthIncludingTrailingWhitespace-frame.width+2));
            s.scrollY=std::clamp(s.scrollY,0.f,std::max(0.f,textMetrics.height-frame.height));
            const auto style=GetWindowLongPtrW(s.hwnd,GWL_STYLE);
            if(style&WS_VSCROLL){
                SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,
                    static_cast<int>(std::ceil(textMetrics.height)),static_cast<UINT>(frame.height),static_cast<int>(s.scrollY),0};
                SetScrollInfo(s.hwnd,SB_VERT,&info,TRUE);
            }
            if(style&WS_HSCROLL){
                SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,
                    static_cast<int>(std::ceil(textMetrics.widthIncludingTrailingWhitespace)),static_cast<UINT>(frame.width),static_cast<int>(s.scrollX),0};
                SetScrollInfo(s.hwnd,SB_HORZ,&info,TRUE);
            }
            if(GetFocus()==s.hwnd){
                const int height=std::max(1,static_cast<int>(std::ceil(metrics.height)));
                if(s.caretHeight!=static_cast<float>(height)){
                    DestroyCaret();CreateCaret(s.hwnd,nullptr,1,height);ShowCaret(s.hwnd);s.caretHeight=static_cast<float>(height);
                }
                POINT caret{static_cast<LONG>(std::lround(frame.rect.left+x-s.scrollX)),
                    static_cast<LONG>(std::lround(frame.rect.top+y-s.scrollY))};
                SetCaretPos(caret.x,caret.y);
                if(const auto context=ImmGetContext(s.hwnd)){
                    COMPOSITIONFORM composition{};composition.dwStyle=CFS_POINT;composition.ptCurrentPos=caret;
                    ImmSetCompositionWindow(context,&composition);
                    CANDIDATEFORM candidate{};candidate.dwStyle=CFS_EXCLUDE;candidate.ptCurrentPos=caret;
                    candidate.rcArea={caret.x,caret.y,caret.x+1,caret.y+height};ImmSetCandidateWindow(context,&candidate);
                    ImmReleaseContext(s.hwnd,context);
                }
            }
        }
    }
    s.syncing=false;
}
D2D1_COLOR_F Color(COLORREF color){return D2D1::ColorF(GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f);}
bool Render(State& s,HDC dc){
    Frame frame;if(!frame.Build(s))return false;
    RECT client{};GetClientRect(s.hwnd,&client);if(client.right<=0||client.bottom<=0)return true;
    const int saved=SaveDC(dc);
    SetTextColor(dc,GetSysColor(IsWindowEnabled(s.hwnd)?COLOR_WINDOWTEXT:COLOR_GRAYTEXT));
    SetBkColor(dc,GetSysColor(s.kind==Kind::Edit?COLOR_WINDOW:COLOR_BTNFACE));
    const auto colorMessage=s.kind==Kind::Edit&&IsWindowEnabled(s.hwnd)&&!(GetWindowLongPtrW(s.hwnd,GWL_STYLE)&ES_READONLY)?
        WM_CTLCOLOREDIT:s.kind==Kind::Button?WM_CTLCOLORBTN:WM_CTLCOLORSTATIC;
    const auto owner=GetParent(s.hwnd);
    auto background=owner?reinterpret_cast<HBRUSH>(SendMessageW(owner,colorMessage,reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(s.hwnd))):nullptr;
    if(!s.alive){RestoreDC(dc,saved);return false;}
    if(!background)background=GetSysColorBrush(s.kind==Kind::Edit?COLOR_WINDOW:COLOR_BTNFACE);
    FillRect(dc,&client,background);
    if(s.kind==Kind::Button){
        const auto state=s.Native(BM_GETSTATE);const bool enabled=IsWindowEnabled(s.hwnd)!=FALSE;
        const auto type=GetWindowLongPtrW(s.hwnd,GWL_STYLE)&BS_TYPEMASK;
        const bool checkbox=type==BS_AUTOCHECKBOX||type==BS_CHECKBOX;
        RECT glyph=client;
        if(checkbox){const LONG side=std::min<LONG>(client.bottom-client.top,20);glyph.right=glyph.left+side;glyph.top+=(client.bottom-client.top-side)/2;glyph.bottom=glyph.top+side;}
        const auto theme=OpenThemeData(s.hwnd,L"BUTTON");
        if(theme){
            int visual=!enabled?PBS_DISABLED:(state&BST_PUSHED)?PBS_PRESSED:(state&BST_HOT)?PBS_HOT:
                ((GetWindowLongPtrW(s.hwnd,GWL_STYLE)&BS_TYPEMASK)==BS_DEFPUSHBUTTON)?PBS_DEFAULTED:PBS_NORMAL;
            if(checkbox){visual=!enabled?CBS_UNCHECKEDDISABLED:(state&BST_PUSHED)?CBS_UNCHECKEDPRESSED:(state&BST_HOT)?CBS_UNCHECKEDHOT:CBS_UNCHECKEDNORMAL;if(s.Native(BM_GETCHECK)==BST_CHECKED)visual+=4;}
            DrawThemeBackground(theme,dc,checkbox?BP_CHECKBOX:BP_PUSHBUTTON,visual,&glyph,nullptr);CloseThemeData(theme);
        }else DrawFrameControl(dc,&glyph,DFC_BUTTON,(checkbox?DFCS_BUTTONCHECK:DFCS_BUTTONPUSH)|
            (!enabled?DFCS_INACTIVE:0)|((state&BST_PUSHED)?DFCS_PUSHED:0)|
            (checkbox&&s.Native(BM_GETCHECK)==BST_CHECKED?DFCS_CHECKED:0));
    }
    const auto textColor=GetTextColor(dc);
    RECT clip{};const auto clipType=GetClipBox(dc,&clip);
    if(clipType==NULLREGION){RestoreDC(dc,saved);return true;}
    if(FAILED(s.target->BindDC(dc,&client))){RestoreDC(dc,saved);return false;}
    Com<ID2D1SolidColorBrush> ink,selection,selectedInk;
    if(FAILED(s.target->CreateSolidColorBrush(Color(frame.placeholder?GetSysColor(COLOR_GRAYTEXT):textColor),ink.Out()))){RestoreDC(dc,saved);return false;}
    s.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    s.target->BeginDraw();
    s.target->PushAxisAlignedClip(D2D1::RectF(static_cast<float>(std::max(clip.left,frame.rect.left)),
        static_cast<float>(std::max(clip.top,frame.rect.top)),static_cast<float>(std::min(clip.right,frame.rect.right)),
        static_cast<float>(std::min(clip.bottom,frame.rect.bottom))),D2D1_ANTIALIAS_MODE_ALIASED);
    const float x=frame.rect.left-s.scrollX,y=frame.rect.top-s.scrollY;
    DWORD start=0,end=0;
    if(s.kind==Kind::Edit)s.Native(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    if(start!=end&&(GetFocus()==s.hwnd||(GetWindowLongPtrW(s.hwnd,GWL_STYLE)&ES_NOHIDESEL))){
        s.target->CreateSolidColorBrush(Color(GetSysColor(COLOR_HIGHLIGHT)),selection.Out());
        s.target->CreateSolidColorBrush(Color(GetSysColor(COLOR_HIGHLIGHTTEXT)),selectedInk.Out());
        if(selection.value&&selectedInk.value){
            UINT32 count=0;frame.layout->HitTestTextRange(start,end-start,x,y,nullptr,0,&count);
            std::vector<DWRITE_HIT_TEST_METRICS> ranges(count);
            if(count&&SUCCEEDED(frame.layout->HitTestTextRange(start,end-start,x,y,ranges.data(),count,&count)))
                for(const auto& r:ranges)s.target->FillRectangle(D2D1::RectF(r.left,r.top,r.left+r.width,r.top+r.height),selection.value);
            frame.layout->SetDrawingEffect(selectedInk.value,{start,end-start});
        }
    }
    s.target->DrawTextLayout(D2D1::Point2F(x,y),frame.layout.value,ink.value,D2D1_DRAW_TEXT_OPTIONS_NONE);
    s.target->PopAxisAlignedClip();
    const bool result=SUCCEEDED(s.target->EndDraw());
    if(s.kind==Kind::Button&&(s.Native(BM_GETSTATE)&BST_FOCUS)&&!(s.Native(WM_QUERYUISTATE)&UISF_HIDEFOCUS)){
        RECT focus=client;InflateRect(&focus,-4,-4);DrawFocusRect(dc,&focus);
    }
    RestoreDC(dc,saved);return result;
}
void ReadComposition(State& s){
    const auto context=ImmGetContext(s.hwnd);if(!context)return;
    const LONG bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);
    if(bytes>=0){s.composition.resize(static_cast<size_t>(bytes)/sizeof(wchar_t));
        if(bytes)ImmGetCompositionStringW(context,GCS_COMPSTR,s.composition.data(),static_cast<DWORD>(bytes));}
    const LONG cursor=ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0);
    s.compositionCursor=cursor>=0?static_cast<UINT32>(cursor):static_cast<UINT32>(s.composition.size());
    ImmReleaseContext(s.hwnd,context);
}
void Invalidate(State& s){if(s.alive&&s.redraw)InvalidateRect(s.hwnd,nullptr,FALSE);}
bool ChangesDisplay(UINT message){
    switch(message){
    case WM_SETTEXT:case WM_SETFONT:case WM_STYLECHANGED:case WM_SIZE:case WM_ENABLE:
    case WM_CHAR:case WM_KEYDOWN:case WM_KEYUP:case WM_CUT:case WM_PASTE:case WM_CLEAR:
    case WM_UNDO:case EM_UNDO:case EM_REPLACESEL:case EM_SETSEL:case EM_SETPASSWORDCHAR:
    case EM_SETRECT:case EM_SETRECTNP:case EM_SETMARGINS:case WM_IME_CHAR:
    case WM_IME_STARTCOMPOSITION:case WM_IME_COMPOSITION:case WM_IME_ENDCOMPOSITION:
    case WM_SETFOCUS:case WM_KILLFOCUS:case WM_LBUTTONDOWN:case WM_LBUTTONUP:
    case WM_MOUSEMOVE:case WM_MOUSELEAVE:case BM_SETSTATE:case BM_SETSTYLE:case BM_SETCHECK:return true;
    default:return false;
    }
}
LRESULT CALLBACK Procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    auto* state=reinterpret_cast<State*>(GetPropW(hwnd,kProperty));
    if(!state)return DefWindowProcW(hwnd,message,w,l);
    Reference reference(state);auto& s=*state;
    if(message==WM_NCDESTROY){
        s.alive=false;RemovePropW(hwnd,kProperty);
        const auto result=s.Native(message,w,l);s.Release();return result;
    }
    if(message==WM_SETREDRAW){s.redraw=w!=0;if(s.redraw)Invalidate(s);return 0;}
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT||message==WM_PRINTCLIENT){
        PAINTSTRUCT paint{};const auto dc=message==WM_PAINT?BeginPaint(hwnd,&paint):reinterpret_cast<HDC>(w);
        // No native text renderer is ever called, including failure paths.
        const bool rendered=dc&&Render(s,dc);
        if(message==WM_PAINT)EndPaint(hwnd,&paint);
        return rendered?1:0;
    }
    if(s.kind==Kind::Edit){
        if(message==EM_GETLINECOUNT||message==EM_LINEINDEX||message==EM_LINEFROMCHAR||
            message==EM_LINELENGTH||message==EM_GETLINE){
            Frame frame;if(!frame.Build(s))return 0;
            UINT32 count=0;frame.layout->GetLineMetrics(nullptr,0,&count);
            std::vector<DWRITE_LINE_METRICS> lines(count);
            if(!count||FAILED(frame.layout->GetLineMetrics(lines.data(),count,&count)))return 0;
            if(message==EM_GETLINECOUNT)return count;
            const UINT32 position=static_cast<int>(w)<0?s.active:std::min(static_cast<UINT32>(w),frame.nativeLength);
            UINT32 currentLine=0,lineStart=0;
            for(UINT32 i=0;i+1<count;++i){if(position<lineStart+lines[i].length)break;lineStart+=lines[i].length;++currentLine;}
            if(message==EM_LINEFROMCHAR)return currentLine;
            if(message==EM_LINELENGTH){
                if(static_cast<int>(w)<0){
                    DWORD start=0,end=0;s.Native(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
                    UINT32 length=0,offset=0;
                    for(const auto& line:lines){const auto tail=offset+line.length-line.newlineLength;
                        if(offset<=end&&offset+line.length>start){const auto a=std::max<UINT32>(offset,start),b=std::min<UINT32>(tail,end);length+=line.length-line.newlineLength-(b>a?b-a:0);}
                        offset+=line.length;
                    }
                    return length;
                }
                return lines[currentLine].length-lines[currentLine].newlineLength;
            }
            const UINT32 requested=static_cast<int>(w)<0?currentLine:static_cast<UINT32>(w);
            if(requested>=count)return message==EM_LINEINDEX?-1:0;
            lineStart=0;for(UINT32 i=0;i<requested;++i)lineStart+=lines[i].length;
            if(message==EM_LINEINDEX)return lineStart;
            if(!l)return 0;
            const auto capacity=*reinterpret_cast<const WORD*>(l);
            const auto length=std::min<UINT32>(capacity,lines[requested].length-lines[requested].newlineLength);
            std::copy_n(frame.text.data()+lineStart,length,reinterpret_cast<wchar_t*>(l));return length;
        }
        if(message==EM_SETCUEBANNER){s.cue=l?reinterpret_cast<const wchar_t*>(l):L"";s.cueWhenFocused=w!=0;Invalidate(s);return s.Native(message,w,l);}
        if(message==WM_CONTEXTMENU){
            DWORD start=0,end=0;s.Native(EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
            const bool editable=IsWindowEnabled(hwnd)&&!(GetWindowLongPtrW(hwnd,GWL_STYLE)&ES_READONLY);
            const bool selected=start!=end;
            const bool password=(GetWindowLongPtrW(hwnd,GWL_STYLE)&ES_PASSWORD)!=0;
            POINT position{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
            if(position.x==-1&&position.y==-1){GetCaretPos(&position);ClientToScreen(hwnd,&position);}
            const std::vector<LumeWidgetMenu::Action> actions{
                {L"撤销",1,editable&&s.Native(EM_CANUNDO)!=0},
                {L"剪切",2,editable&&selected&&!password},{L"复制",3,selected&&!password},
                {L"粘贴",4,editable&&IsClipboardFormatAvailable(CF_UNICODETEXT)!=FALSE},
                {L"删除",5,editable&&selected},{L"全选",6,s.Native(WM_GETTEXTLENGTH)>0}};
            const auto command=LumeWidgetMenu::ShowItems(hwnd,position,actions);
            if(!s.alive)return 0;
            switch(command){case 1:SendMessageW(hwnd,WM_UNDO,0,0);break;case 2:SendMessageW(hwnd,WM_CUT,0,0);break;
            case 3:SendMessageW(hwnd,WM_COPY,0,0);break;case 4:SendMessageW(hwnd,WM_PASTE,0,0);break;
            case 5:SendMessageW(hwnd,WM_CLEAR,0,0);break;case 6:SendMessageW(hwnd,EM_SETSEL,0,-1);break;default:break;}
            return 0;
        }
        if(message==EM_POSFROMCHAR||message==EM_CHARFROMPOS){
            Frame frame;if(!frame.Build(s))return -1;
            if(message==EM_POSFROMCHAR){
                if(w>frame.nativeLength)return -1;
                float x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
                if(!frame.Point(frame.DisplayPosition(s,static_cast<UINT32>(w)),x,y,metrics))return -1;
                return MAKELONG(static_cast<short>(std::lround(frame.rect.left+x-s.scrollX)),
                    static_cast<short>(std::lround(frame.rect.top+y-s.scrollY)));
            }
            const auto index=frame.Hit(s,static_cast<float>(GET_X_LPARAM(l)),static_cast<float>(GET_Y_LPARAM(l)));
            float x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};frame.Point(index,x,y,metrics);
            return MAKELONG(static_cast<WORD>(index),static_cast<WORD>(y/std::max(1.f,metrics.height)));
        }
        if(message==EM_GETFIRSTVISIBLELINE){
            if(s.multiline)return static_cast<LRESULT>(s.scrollY/std::max(1.f,s.lineHeight));
            Frame frame;if(!frame.Build(s))return 0;
            return frame.Hit(s,static_cast<float>(frame.rect.left),static_cast<float>(frame.rect.top));
        }
        if(message==EM_SCROLLCARET){Selection(s);Sync(s,true);Invalidate(s);return 0;}
        if(message==EM_LINESCROLL||message==WM_MOUSEWHEEL||message==WM_VSCROLL||message==WM_HSCROLL||message==EM_SCROLL){
            Frame frame;if(!frame.Build(s))return 0;
            if(message==EM_LINESCROLL){s.scrollX+=static_cast<int>(w)*s.lineHeight*.5f;s.scrollY+=static_cast<int>(l)*s.lineHeight;}
            else if(message==WM_MOUSEWHEEL)s.scrollY-=GET_WHEEL_DELTA_WPARAM(w)/static_cast<float>(WHEEL_DELTA)*3*s.lineHeight;
            else{
                auto& offset=message==WM_HSCROLL?s.scrollX:s.scrollY;
                const float page=message==WM_HSCROLL?frame.width:frame.height;
                switch(LOWORD(w)){
                case SB_LINEUP:offset-=s.lineHeight;break;case SB_LINEDOWN:offset+=s.lineHeight;break;
                case SB_PAGEUP:offset-=page;break;case SB_PAGEDOWN:offset+=page;break;
                case SB_TOP:offset=0;break;case SB_BOTTOM:offset=10000000;break;
                case SB_THUMBTRACK:case SB_THUMBPOSITION:offset=static_cast<float>(HIWORD(w));break;default:break;
                }
            }
            Sync(s,false);Invalidate(s);return message==EM_SCROLL?MAKELONG(0,TRUE):TRUE;
        }
        if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||
            (message==WM_MOUSEMOVE&&s.dragging)){
            if(message!=WM_MOUSEMOVE){SetFocus(hwnd);if(!s.alive)return 0;SetCapture(hwnd);s.dragging=true;Selection(s);}
            Frame frame;if(!frame.Build(s))return 0;
            UINT32 index=frame.Hit(s,static_cast<float>(GET_X_LPARAM(l)),static_cast<float>(GET_Y_LPARAM(l)));
            if(message==WM_LBUTTONDBLCLK){
                BOOL trailing=FALSE,inside=FALSE;DWRITE_HIT_TEST_METRICS cluster{};
                frame.layout->HitTestPoint(GET_X_LPARAM(l)-frame.rect.left+s.scrollX,
                    GET_Y_LPARAM(l)-frame.rect.top+s.scrollY,&trailing,&inside,&cluster);
                const auto length=static_cast<UINT32>(frame.text.size());
                UINT32 a=std::min(cluster.textPosition,length);
                UINT32 b=a+std::min(cluster.length,length-a);
                const auto word=[](wchar_t c){
                    WORD type=0,marks=0;
                    GetStringTypeW(CT_CTYPE1,&c,1,&type);
                    GetStringTypeW(CT_CTYPE3,&c,1,&marks);
                    // Font fallback may shape a combining mark in its own
                    // cluster. It still belongs to the surrounding word.
                    return (type&(C1_ALPHA|C1_DIGIT))!=0||c==L'_'||
                        (marks&(C3_NONSPACING|C3_DIACRITIC|C3_VOWELMARK))!=0;
                };
                // Expand only a word cluster. Punctuation and emoji select
                // their own DirectWrite cluster, without swallowing a word
                // immediately before or after it.
                if(a<length&&word(frame.text[a])){
                    float x=0,y=0;DWRITE_HIT_TEST_METRICS adjacent{};
                    while(a>0&&frame.Point(a-1,x,y,adjacent)){
                        if(adjacent.textPosition>=a||!word(frame.text[adjacent.textPosition]))break;
                        a=adjacent.textPosition;
                    }
                    while(b<length&&frame.Point(b,x,y,adjacent)){
                        const auto next=adjacent.textPosition+adjacent.length;
                        if(next<=b||adjacent.textPosition>=length||!word(frame.text[adjacent.textPosition]))break;
                        b=std::min(next,length);
                    }
                }
                SetSelection(s,frame.NativePosition(s,a),frame.NativePosition(s,b));
            }else{
                const UINT32 anchor=message==WM_MOUSEMOVE||(GetKeyState(VK_SHIFT)&0x8000)?s.anchor:index;
                SetSelection(s,anchor,index);
            }
            Sync(s,true);Invalidate(s);return 0;
        }
        if(message==WM_LBUTTONUP&&s.dragging){s.dragging=false;if(GetCapture()==hwnd)ReleaseCapture();return 0;}
        if(message==WM_CAPTURECHANGED)s.dragging=false;
        if(message==WM_KEYDOWN&&w=='A'&&(GetKeyState(VK_CONTROL)&0x8000)){
            SetSelection(s,0,static_cast<UINT32>(s.Native(WM_GETTEXTLENGTH)));Sync(s,true);Invalidate(s);return 0;
        }
        if(message==WM_KEYDOWN&&!s.composing&&s.multiline&&
            (w==VK_UP||w==VK_DOWN||w==VK_PRIOR||w==VK_NEXT||w==VK_HOME||w==VK_END)&&!(GetKeyState(VK_CONTROL)&0x8000)){
            Frame frame;if(!frame.Build(s))return 0;
            Selection(s);float x=0,y=0;DWRITE_HIT_TEST_METRICS metrics{};
            if(frame.Point(s.active,x,y,metrics)){
                if(w==VK_UP)y-=metrics.height;else if(w==VK_DOWN)y+=metrics.height;
                else if(w==VK_PRIOR)y-=frame.height;else if(w==VK_NEXT)y+=frame.height;
                else x=w==VK_HOME?-10000000.f:10000000.f;
                const auto index=frame.Hit(s,frame.rect.left+x-s.scrollX,frame.rect.top+y+metrics.height*.5f-s.scrollY);
                SetSelection(s,(GetKeyState(VK_SHIFT)&0x8000)?s.anchor:index,index);Sync(s,true);Invalidate(s);
            }
            return 0;
        }
        if(message==WM_IME_STARTCOMPOSITION){Selection(s);s.composing=true;s.compositionStart=std::min(s.anchor,s.active);}
        if(message==WM_IME_COMPOSITION){
            if(l&GCS_RESULTSTR){s.composition.clear();s.compositionCursor=0;}
            else if(l&GCS_COMPSTR)ReadComposition(s);
        }
        if(message==WM_IME_ENDCOMPOSITION){s.composing=false;s.composition.clear();}
    }
    const auto result=s.Native(message,w,l);
    if(!s.alive)return result;
    if(ChangesDisplay(message)){
        if(s.kind==Kind::Edit){
            if(message==WM_SETFOCUS)s.caretHeight=0;
            if(message==WM_KILLFOCUS)s.caretHeight=0;
            Selection(s);
            if(message==WM_IME_COMPOSITION&&(l&GCS_RESULTSTR)){
                s.compositionStart=s.active;
                if(l&GCS_COMPSTR)ReadComposition(s);
            }
            if(message==EM_SETSEL&&static_cast<int>(w)>=0){
                const auto length=static_cast<UINT32>(s.Native(WM_GETTEXTLENGTH));
                s.anchor=std::min(static_cast<UINT32>(w),length);
                s.active=static_cast<int>(l)<0?length:std::min(static_cast<UINT32>(l),length);
            }
            Sync(s,message!=WM_MOUSEMOVE&&message!=WM_KILLFOCUS);
        }
        Invalidate(s);
    }
    return result;
}
}
bool Attach(HWND control){
    if(!IsWindow(control))return false;
    if(GetPropW(control,kProperty))return true;
    wchar_t name[32]{};GetClassNameW(control,name,32);
    Kind kind=Kind::Edit;
    const auto style=GetWindowLongPtrW(control,GWL_STYLE);
    if(_wcsicmp(name,L"EDIT")==0)kind=Kind::Edit;
    else if(_wcsicmp(name,L"STATIC")==0&&((style&SS_TYPEMASK)==SS_LEFT||(style&SS_TYPEMASK)==SS_CENTER||(style&SS_TYPEMASK)==SS_RIGHT))kind=Kind::Label;
    else if(_wcsicmp(name,L"BUTTON")==0&&((style&BS_TYPEMASK)==BS_PUSHBUTTON||(style&BS_TYPEMASK)==BS_DEFPUSHBUTTON||
        (style&BS_TYPEMASK)==BS_CHECKBOX||(style&BS_TYPEMASK)==BS_AUTOCHECKBOX))kind=Kind::Button;
    else return false;
    auto state=std::make_unique<State>();auto& s=*state;
    s.hwnd=control;s.kind=kind;s.multiline=kind==Kind::Edit&&(style&ES_MULTILINE);
    if(FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(s.write.Out()))))return false;
    if(FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,s.draw.Out())))return false;
    // Font handles and HWND client rectangles are already in device pixels.
    // Keeping this target at 96 DPI avoids applying window scale twice.
    const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
    if(FAILED(s.draw->CreateDCRenderTarget(&properties,s.target.Out())))return false;
    s.previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(control,GWLP_WNDPROC));
    Frame frame;if(!frame.Build(s))return false;
    if(!SetPropW(control,kProperty,state.get()))return false;
    SetLastError(0);
    if(!SetWindowLongPtrW(control,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure))&&GetLastError()){
        RemovePropW(control,kProperty);return false;
    }
    // EDIT also draws directly during WM_CHAR/selection/IME handling. Disable
    // its internal renderer permanently, rather than painting over GDI glyphs.
    // DefWindowProc may remove WS_VISIBLE for WM_SETREDRAW; restore the style
    // without re-enabling the original EDIT's redraw flag. Our procedure owns
    // WM_PAINT and WM_PRINTCLIENT independently of that flag.
    s.Native(WM_SETREDRAW,FALSE);
    if(style&WS_VISIBLE)SetWindowLongPtrW(control,GWL_STYLE,GetWindowLongPtrW(control,GWL_STYLE)|WS_VISIBLE);
    if(kind==Kind::Edit){Selection(s);Sync(s,true);}
    state.release();InvalidateRect(control,nullptr,FALSE);return true;
}
}
