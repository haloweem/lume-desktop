#include "core/screenshot/editor_model.h"
#include <cstdlib>
#include <iostream>
using namespace lume::screenshot;
void Check(bool ok,const char* what){if(!ok){std::cerr<<what<<'\n';std::exit(1);}}
int main(){
    Annotation options;
    Check(options.fontFamily==L"Microsoft YaHei UI"&&options.fontSize==0&&!options.bold&&!options.italic&&!options.outline&&options.brushSize==0,"new annotation options preserve defaults");
    options.tool=Tool::Text;options.points={{10,10}};options.text=L"WW\nWW";options.fontSize=60;options.bold=true;options.italic=true;options.outline=true;options.fontFamily=L"Courier New";
    Check(Hit(options,{60,140},0),"explicit font size expands multiline text hit envelope");
    options.fontSize=8;Check(!Hit(options,{60,140},0),"small explicit text size does not use legacy width");
    History fonts;options.fontSize=-1;Check(!fonts.Add(options),"negative font size rejected");
    options.fontSize=201;Check(!fonts.Add(options),"font size above 200 rejected");
    options.fontSize=200;options.fontFamily.assign(129,L'a');Check(!fonts.Add(options),"oversized font family rejected");
    options.fontFamily.assign(128,L'a');Check(fonts.Add(options),"font size and family boundary accepted");
    Check(fonts.Items().back().outline&&fonts.Items().back().bold&&fonts.Items().back().italic,"history retains text styles");
    Check(fonts.Undo()&&fonts.Redo()&&fonts.Items().back().fontFamily.size()==128&&fonts.Items().back().fontSize==200,"undo redo preserves font options");
    options.tool=Tool::Mosaic;options.variant=2;options.width=20;options.brushSize=10;options.points={{50,50},{100,50}};
    Check(AnnotationBrushRadius(options)==5&&Hit(options,{75,55},0)&&!Hit(options,{75,56},0),"explicit brush size decouples coverage from strength");
    options.brushSize=1;Check(AnnotationBrushRadius(options)==1,"one-pixel size retains minimum radius");
    options.brushSize=0;Check(AnnotationBrushRadius(options)==100,"zero brush size preserves width-derived radius");
    const Rect bounds{0,0,800,600};
    Check(Normalize({220,180},{20,30})==Rect{20,30,220,180},"reverse selection normalizes");
    Check(Constrain({-10,-20,900,700},bounds)==bounds,"selection clamps to virtual image");
    Rect selection{100,100,300,250};
    Check(HitHandle(selection,{100,100},6)==(Left|Top),"corner handle");
    Check(HitHandle(selection,{200,100},6)==Top,"edge midpoint handle");
    Check(HitHandle(selection,{200,180},6)==Move,"inside moves selection");
    Check(Drag(selection,Move,{1000,1000},bounds)==Rect{600,450,800,600},"move keeps size and clamps");
    Check(Drag(selection,Left|Top,{250,200},bounds)==Rect{300,250,350,300},"crossed handles normalize");
    History history;
    Annotation a;a.tool=Tool::Arrow;a.points={{10,10},{100,100}};
    Check(history.Add(a)&&history.Items().size()==1,"annotation commits");
    auto adjusted=a;adjusted.color=0x00ff00;adjusted.width=12;
    Check(history.UpdateLast(adjusted)&&history.Items().size()==1&&history.Items().back().color==0x00ff00&&history.Items().back().width==12,"latest annotation updates in place");
    Check(history.Undo()&&history.Items().empty()&&history.CanRedo(),"undo removes annotation");
    Check(history.Redo()&&history.Items().size()==1,"redo restores annotation");
    Annotation eraser;eraser.tool=Tool::Eraser;eraser.brushSize=12;eraser.points={{40,50},{60,50}};
    Check(history.Add(eraser)&&history.Items().size()==2&&history.Items().back().tool==Tool::Eraser,"eraser stroke is retained as a pixel operation instead of deleting an annotation");
    Check(history.Undo()&&history.Items().size()==1,"undo removes one eraser stroke without removing the shape");
    Check(history.Redo()&&history.Items().size()==2,"redo restores the pixel eraser stroke");
    history.Undo();history.Undo();a.tool=Tool::Pen;Check(history.Add(a)&&!history.CanRedo(),"new branch clears redo");
    a.points.clear();Check(!history.Add(a),"empty annotation rejected");
    Annotation marker;marker.tool=Tool::Marker;marker.width=5;marker.points={{10,20},{100,20}};
    Check(Hit(marker,{50,30},1),"eraser hits full visible marker thickness");
    Annotation mosaic;mosaic.tool=Tool::Mosaic;mosaic.width=3;mosaic.points={{40,40},{140,40},{140,140}};
    Check(MosaicBrushRadius(0)==8&&MosaicBrushRadius(1)==8&&MosaicBrushRadius(3)==15,"mosaic radius has minimum and scales with width");
    for(int variant:{2,3}){
        mosaic.variant=variant;
        Check(Hit(mosaic,{90,53},0),"mosaic brush hit follows intermediate segments and radius");
        Check(Hit(mosaic,{27,40},0),"mosaic brush hit includes round starting cap");
        Check(!Hit(mosaic,{70,100},0),"mosaic brush does not hit empty bounding rectangle");
        Check(!Hit(mosaic,{90,60},0)&&Hit(mosaic,{90,60},6),"mosaic brush eraser tolerance extends radius");
    }
    mosaic.variant=0;Check(Hit(mosaic,{70,100},0),"rectangle mosaic keeps rectangle hit semantics");
    Annotation multiline;multiline.tool=Tool::Text;multiline.width=3;multiline.points={{10,10}};multiline.text=L"one\ntwo\nthree\nfour";
    Check(Hit(multiline,{20,100},1),"eraser hits later visible text lines");
    History longStrokes;Annotation stroke;stroke.points.resize(20000,{10,10});stroke.fontFamily.assign(128,L'f');
    for(int i=0;i<70;++i)Check(longStrokes.Add(stroke),"bounded long-stroke document accepted");
    int undoCount=0;while(longStrokes.Undo())++undoCount;
    Check(undoCount>0&&undoCount<10,"deep stroke history is memory-bounded, not just operation-bounded");
    std::size_t retained=0;int expectedStates=0;
    for(int count=70;count>=0;--count){
        const auto bytes=sizeof(std::vector<Annotation>)+static_cast<std::size_t>(count)*(sizeof(Annotation)+stroke.points.size()*sizeof(Point)+stroke.fontFamily.size()*sizeof(wchar_t));
        if(retained+bytes>32u*1024u*1024u)break;
        retained+=bytes;++expectedStates;
    }
    Check(undoCount==expectedStates-1,"history memory accounting includes font-family storage");
    Check(ToolbarBounds({730,550,790,590},bounds,540,40,8).right<=800,"toolbar clamped horizontally");
    Check(ToolbarBounds({730,550,790,590},bounds,540,40,8).bottom<=550,"toolbar flips above lower selection");
    std::cout<<"PASS screenshot geometry, handles, bounded history, eraser and toolbar\n";
}
