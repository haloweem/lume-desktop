// Copies display metadata only; the caller owns notices and operation lifetime.
export async function copySongInfo(document,track,{signal}={}) {
  if(!track || signal?.aborted)return false;
  const text=['title','artist','album'].map(key=>typeof track[key]==='string'?track[key]:'').join(' - ');
  try {
    const window=document.defaultView;
    if(window?.isSecureContext && typeof window.navigator?.clipboard?.writeText==='function') {
      // A submitted browser write cannot be cancelled; preserve its actual result.
      await window.navigator.clipboard.writeText(text);
      return true;
    }
  } catch { /* Clipboard denial may still allow a synchronous copy command. */ }

  let textarea,active,inputSelection,selection,ranges,anchor,focus;
  try {
    if(signal?.aborted || !document.body || typeof document.execCommand!=='function')return false;
    // Capture immediately before fallback, not before an asynchronous permission wait.
    active=document.activeElement;
    if(typeof active?.selectionStart==='number')
      inputSelection=[active.selectionStart,active.selectionEnd,active.selectionDirection];
    selection=document.getSelection();
    ranges=selection?Array.from({length:selection.rangeCount},(_,index)=>selection.getRangeAt(index).cloneRange()):[];
    anchor=selection?.anchorNode?[selection.anchorNode,selection.anchorOffset]:null;
    focus=selection?.focusNode?[selection.focusNode,selection.focusOffset]:null;
    textarea=document.createElement('textarea');
    textarea.value=text;
    textarea.readOnly=true;
    textarea.tabIndex=-1;
    textarea.style.cssText='position:fixed;top:0;left:0;opacity:0;pointer-events:none;';
    document.body.append(textarea);
    textarea.focus({preventScroll:true});
    if(signal?.aborted)return false;
    textarea.select();
    if(signal?.aborted)return false;
    return document.execCommand('copy')===true;
  } catch {
    return false;
  } finally {
    textarea?.remove();
    try {
      if(!signal?.aborted && active?.isConnected)active.focus({preventScroll:true});
    } catch { /* A disappearing control must not prevent selection cleanup. */ }
    try {
      if(!signal?.aborted && selection && ranges) {
        selection.removeAllRanges();
        for(const range of ranges)
          if(range.startContainer.isConnected && range.endContainer.isConnected)selection.addRange(range);
        if(ranges.length===1 && anchor?.[0].isConnected && focus?.[0].isConnected &&
          typeof selection.setBaseAndExtent==='function')
          selection.setBaseAndExtent(...anchor,...focus);
      }
    } catch { /* The selected content may have been removed by a copy handler. */ }
    try {
      if(!signal?.aborted && active?.isConnected && inputSelection)active.setSelectionRange(...inputSelection);
    } catch { /* Some input types do not support restoring a text selection. */ }
  }
}
