// The slider itself moves when the whole window scales. Keep the drag track in
// physical client pixels so resizing cannot feed back into the pointer value.
export function mountInterfaceScaleControl(field,{onPreview,onCommit}) {
  const view=field.ownerDocument.defaultView;
  const slider=field.querySelector('[data-setting-slider]');
  const number=field.querySelector('[data-setting-number]');
  const output=field.querySelector('output');
  let confirmed=75,shown=75,disabled=true,drag=null,frame=0,lastPreview=null,disposed=false;
  const valid=value=>Number.isInteger(value)&&value>=50&&value<=75;
  function paint() {
    slider.value=String(shown);number.value=String(shown);output.textContent=`${shown}%`;
    slider.setAttribute('aria-valuetext',`${shown}%`);
    slider.style.setProperty('--range-progress',`${(shown-50)*4}%`);
  }
  function flush() {
    if(frame)view.cancelAnimationFrame(frame);
    frame=0;
    if(lastPreview===shown)return;
    if(onPreview(shown)===false){shown=confirmed;paint();return;}
    lastPreview=shown;
  }
  function preview(value) {
    if(!valid(value)||disabled||disposed)return;
    shown=value;paint();
    if(!frame)frame=view.requestAnimationFrame(flush);
  }
  function release() {
    const active=drag;drag=null;
    if(active&&slider.hasPointerCapture?.(active.id))slider.releasePointerCapture(active.id);
  }
  function cancel() {
    const needsRestore=shown!==confirmed||(lastPreview!==null&&lastPreview!==confirmed)||frame;
    release();shown=confirmed;paint();if(needsRestore)flush();
  }
  function commit() {
    if(disabled||disposed)return;
    release();flush();
    if(shown!==confirmed && onCommit(shown)===false)cancel();
  }
  const pointerValue=event=>Math.max(50,Math.min(75,Math.round(50+
    (event.clientX*(view.devicePixelRatio||1)-drag.left-drag.grab)/drag.width*25)));
  function down(event) {
    if(event.target!==slider||event.button!==0)return;
    if(disabled){event.preventDefault();return;}
    event.preventDefault();slider.focus({preventScroll:true});
    const rect=slider.getBoundingClientRect(),dpi=view.devicePixelRatio||1;
    const left=(rect.left+8)*dpi,width=Math.max(1,(rect.width-16)*dpi);
    const x=event.clientX*dpi,thumb=left+width*(shown-50)/25;
    drag={id:event.pointerId,left,width,grab:Math.abs(x-thumb)<=12*dpi?x-thumb:0};
    slider.setPointerCapture(event.pointerId);preview(pointerValue(event));
  }
  function move(event) {
    if(!drag||event.pointerId!==drag.id)return;
    event.preventDefault();preview(pointerValue(event));
  }
  function up(event) {
    if(!drag||event.pointerId!==drag.id)return;
    preview(pointerValue(event));commit();
  }
  function lost(event) {if(drag&&event.pointerId===drag.id)cancel();}
  function input(event) {
    if(event.target!==slider&&event.target!==number)return;
    if(disabled){paint();return;}
    if(event.target===number)number.dataset.editing='true';
    const value=event.target.valueAsNumber;
    if(valid(value))preview(value);
  }
  function change(event) {
    if(event.target!==slider&&event.target!==number)return;
    if(disabled){paint();return;}
    const value=event.target.valueAsNumber;
    delete number.dataset.editing;
    if(!valid(value)){cancel();return;}
    preview(value);commit();
  }
  function key(event) {
    if(event.target!==slider&&event.target!==number)return;
    if(disabled&&event.key!=='Tab'){event.preventDefault();return;}
    if(event.key==='Escape'){event.preventDefault();delete number.dataset.editing;cancel();}
    if(event.target===number&&event.key==='Enter'){event.preventDefault();change(event);}
  }
  function blur(event) {
    if(event.target===number&&number.dataset.editing==='true')change(event);
  }
  const events={pointerdown:down,pointermove:move,pointerup:up,pointercancel:lost,
    lostpointercapture:lost,input,change,keydown:key,focusout:blur};
  for(const [type,handler] of Object.entries(events))field.addEventListener(type,handler);
  return {
    sync(value,busy,label) {
      confirmed=valid(value)?value:75;disabled=busy;
      if(busy&&drag)release();
      if(!drag){shown=confirmed;paint();if(!frame)lastPreview=confirmed;}
      for(const control of [slider,number]){
        control.disabled=busy&&field.ownerDocument.activeElement!==control;
        control.setAttribute('aria-disabled',String(busy));
        control.setAttribute('aria-label',label);
      }
      number.readOnly=busy;
      field.querySelector(`label[for="${slider.id}"]`).textContent=label;
      slider.dataset.confirmed=String(confirmed);
    },
    cancel,
    dispose() {
      if(disposed)return;
      cancel();disposed=true;
      for(const [type,handler] of Object.entries(events))field.removeEventListener(type,handler);
    },
  };
}
