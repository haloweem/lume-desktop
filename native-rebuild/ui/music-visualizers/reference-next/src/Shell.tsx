import React,{forwardRef} from 'react';
import {resolveThemeFontStack,resolveThemeFontWeight} from 'reference:fonts';

// Same outer geometry and typography as VisualizerShell; the music page owns wallpaper/chrome.
export default forwardRef(function VisualizerShell({theme,sharedProps,visualizerOpacity=1,children,className=''}:any,ref:any){
  const fontClassName=theme.fontStyle==='mono'?'font-mono':theme.fontStyle==='serif'?'font-serif':'font-sans';
  return <div ref={ref} data-lumenic-liquid-glass-scene
    className={`w-full h-full flex flex-col items-center justify-center overflow-hidden relative ${fontClassName} transition-colors duration-1000 ${className}`.trim()}
    style={{backgroundColor:'transparent',fontFamily:resolveThemeFontStack(theme),
      fontWeight:resolveThemeFontWeight(theme,400),opacity:sharedProps?.visualizerOpacity??visualizerOpacity}}>{children}</div>;
});
