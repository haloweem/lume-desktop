import test from "node:test";
import assert from "node:assert/strict";
import {createRequire} from "node:module";
import {createBridge} from "../bridge.js";
import {mountMusicDrop} from "../music-drop.js";
import {mountWallpaperDrop} from "../wallpaper-drop.js";

test("dropped files use native additional objects, never page-supplied path strings",()=>{
  const messages=[],files=[new File(["one"],"one.png"),new File(["two"],"two.mp4")];
  const bridge=createBridge({postMessageWithAdditionalObjects:(...args)=>messages.push(args)});
  assert.equal(bridge.dropFiles(files),true);
  assert.deepEqual(messages,[["v1|wallpaper|dropFiles",files]]);
  for(const input of [[],["C:\\private.png"],[{name:"fake.png",path:"C:\\private.png"}],
    Array.from({length:33},()=>files[0]),null])
    assert.equal(bridge.dropFiles(input),false);
  assert.equal(messages.length,1);
  assert.equal(createBridge({postMessage(){}}).dropFiles(files),false);
  assert.equal(createBridge({postMessageWithAdditionalObjects(){throw new Error("unavailable");}}).dropFiles(files),false);
});
test("drop accepts multiple files only in the local zone and blocks default file navigation everywhere",()=>{
  const handlers=new Map(),received=[],errors=[];
  let enabled=true,active=false;
  const zone={classList:{toggle:(_,value)=>{active=value;}}};
  const root={
    addEventListener:(name,handler)=>handlers.set(name,handler),
    removeEventListener:(name)=>handlers.delete(name),
  };
  const unmount=mountWallpaperDrop(root,{isEnabled:()=>enabled,
    onFiles:files=>received.push(files),onError:error=>errors.push(error)});
  const files=[new File(["one"],"one.png"),new File(["two"],"two.mp4")];
  function event(local=true,data=files) {
    return {target:{closest:()=>local?zone:null},dataTransfer:{types:["Files"],files:data},
      prevented:false,preventDefault(){this.prevented=true;}};
  }
  const drag=event();
  handlers.get("dragover")(drag);
  assert.equal(drag.prevented,true);
  assert.equal(drag.dataTransfer.dropEffect,"copy");
  assert.equal(active,true);
  const drop=event();
  handlers.get("drop")(drop);
  assert.equal(drop.prevented,true);
  assert.equal(active,false);
  assert.deepEqual(received,[files]);
  const official=event(false);
  handlers.get("drop")(official);
  assert.equal(official.prevented,true);
  enabled=false;
  handlers.get("drop")(event());
  assert.equal(received.length,1);
  enabled=true;
  handlers.get("drop")(event(true,Array.from({length:33},()=>files[0])));
  assert.equal(received.length,1);
  assert.match(errors.at(-1),/32/);
  unmount();
  assert.equal(handlers.size,0);
});
const jsdomPath=process.env.LUME_JSDOM_PATH || process.env.LUME_JSDOM;
const {JSDOM}=jsdomPath ? createRequire(import.meta.url)(jsdomPath) : {};
for (const order of [["wallpaper","music"],["music","wallpaper"]])
  for (const active of ["wallpaper","music","none"])
    test(`joint DOM drops: ${order.join(" then ")}, ${active} active`,{skip:!jsdomPath},t=>{
      const dom=new JSDOM(`
        <section data-wallpaper-drop-zone="local"><span id="wallpaper"></span></section>
        <section data-music-drop-zone="local"><span id="music"></span></section>
        <div id="outside"></div>`,{pretendToBeVisual:true});
      t.after(()=>dom.window.close());
      const {document,Event}=dom.window;
      const files=[new File(["one"],"one.png"),new File(["two"],"two.mp3")];
      const messages=[],errors=[];
      const bridge=createBridge({
        postMessageWithAdditionalObjects:(...args)=>messages.push(args),
      });
      const mounts={wallpaper:mountWallpaperDrop,music:mountMusicDrop};
      const send={wallpaper:batch=>bridge.dropFiles(batch),music:batch=>bridge.musicDropFiles(batch)};
      const unmount=order.map(kind=>mounts[kind](document,{
        isEnabled:()=>active===kind,
        onFiles:batch=>assert.equal(send[kind](batch),true),
        onError:error=>errors.push(error),
      }));
      t.after(()=>unmount.forEach(dispose=>dispose()));
      function dispatch(type,target,initialEffect="copy",types=["Files"],claimed=false) {
        let effect=initialEffect;
        const writes=[];
        const event=new Event(type,{bubbles:true,cancelable:true});
        Object.defineProperty(event,"dataTransfer",{value:{
          types,files,
          get dropEffect(){return effect;},
          set dropEffect(value){writes.push(value);effect=value;},
        }});
        if (claimed) event.preventDefault();
        document.getElementById(target).dispatchEvent(event);
        return {event,writes,effect};
      }
      for (const target of ["wallpaper","music","outside"]) {
        const accepted=target===active, before=messages.length;
        const drag=dispatch("dragover",target);
        assert.equal(drag.event.defaultPrevented,true);
        assert.equal(drag.effect,accepted ? "copy" : "none");
        const decision=accepted ? "copy" : "none";
        const expectedWrites=active==="none" ? ["none"] :
          order[0]===active ? [decision] : ["none",decision];
        assert.deepEqual(drag.writes,expectedWrites,
          "inactive handlers reject unclaimed drags but preserve the active domain decision");
        assert.equal(document.querySelectorAll(".wallpaper-drop-active,.music-drop-active").length,
          accepted ? 1 : 0);
        assert.equal(dispatch("drop",target).event.defaultPrevented,true);
        assert.equal(messages.length,before+(accepted ? 1 : 0));
        assert.equal(document.querySelectorAll(".wallpaper-drop-active,.music-drop-active").length,0);
        if (accepted) {
          assert.equal(messages.at(-1)[0],`v1|${active}|dropFiles`);
          assert.equal(messages.at(-1)[1].length,2);
          files.forEach((file,index)=>assert.strictEqual(messages.at(-1)[1][index],file));
        }
      }
      const beforeHidden=messages.length;
      if (active!=="none") dispatch("dragover",active);
      Object.defineProperty(document,"hidden",{configurable:true,value:true});
      for (const target of ["wallpaper","music","outside"]) {
        assert.equal(dispatch("drop",target).event.defaultPrevented,true,
          "hiding between dragover and drop blocks delivery, but still prevents navigation");
        const drag=dispatch("dragover",target,"copy");
        assert.equal(drag.event.defaultPrevented,true);
        assert.deepEqual(drag.writes,["none"],"hidden handlers reject unclaimed default copy");
        assert.equal(drag.effect,"none");
        const claimed=dispatch("dragover",target,"copy",["Files"],true);
        assert.deepEqual(claimed.writes,[],"inactive handlers preserve an existing acceptance");
        assert.equal(claimed.effect,"copy");
      }
      assert.equal(messages.length,beforeHidden);
      assert.equal(document.querySelectorAll(".wallpaper-drop-active,.music-drop-active").length,0);
      assert.deepEqual(errors,[]);
      assert.equal(dispatch("dragover","outside","move",["text/plain"]).event.defaultPrevented,false);
      unmount.forEach(dispose=>dispose());
      assert.equal(dispatch("drop","wallpaper").event.defaultPrevented,false);
      assert.equal(messages.length,beforeHidden);
    });
