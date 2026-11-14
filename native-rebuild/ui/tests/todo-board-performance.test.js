import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {mountTodoBoards,todoBoardMarkup} from '../todo-board.js';

const jsdomPath=process.env.LUME_JSDOM_PATH;
const {JSDOM}=jsdomPath?createRequire(import.meta.url)(jsdomPath):{};
test('large task snapshots coalesce, retain unchanged cards, and create sub-steps only when opened',
  {skip:!jsdomPath},async t=>{
    const dom=new JSDOM('<main>'+todoBoardMarkup()+'</main>',{pretendToBeVisual:true});
    const {document}=dom.window,root=document.querySelector('main'),frames=new Map(),commands=[];
    let nextFrame=0;
    dom.window.matchMedia=()=>({matches:true});
    dom.window.requestAnimationFrame=callback=>{frames.set(++nextFrame,callback);return nextFrame;};
    dom.window.cancelAnimationFrame=id=>frames.delete(id);
    const flush=()=>{const pending=[...frames.values()];frames.clear();pending.forEach(callback=>callback());};
    const owner=mountTodoBoards(root,{bridge:{todoBoard:(action,fields)=>{commands.push({action,...fields});return true;}}});
    t.after(()=>{owner.dispose();dom.window.close();});
    const task=i=>({id:String(i),title:`Task ${i}`,notes:'',completed:false,repeat:false,color:'none',
      date:'2026-11-12',time:'',end:'',allDay:true,days:1,endDate:'2026-11-12',frequency:'none',reminder:-1,
      subtasks:Array.from({length:20},(_,n)=>({id:`s${n}`,title:`Step ${n}`,completed:false}))});
    const state={version:1,type:'todo-board-state',result:'snapshot',error:'',available:true,writable:true,
      today:'2026-11-12',tasks:Array.from({length:200},(_,i)=>task(i))};
    for(let i=0;i<10;i++)owner.receive(structuredClone(state));
    assert.equal(frames.size,1,'one presentation for a burst of host snapshots');flush();
    assert.equal(root.querySelectorAll('[data-tb-card]').length,200);
    assert.equal(root.querySelectorAll('.tb-sub__list li').length,0,'4000 collapsed steps do not allocate hidden rows');
    const first=root.querySelector('[data-tb-card="0"]');
    owner.receive(structuredClone(state));flush();
    assert.equal(root.querySelector('[data-tb-card="0"]'),first,'unchanged snapshots keep the DOM and its artwork/focus');
    first.querySelector('[data-tb-steps-toggle]').click();
    assert.equal(first.querySelectorAll('.tb-sub__list li').length,20);
    const check=first.querySelector('[data-tb-step-check][data-step="s0"]');check.click();
    assert.equal(check.getAttribute('aria-checked'),'true');
    assert.equal(commands.at(-1).subtasks[0].completed,true);
    owner.receive({...structuredClone(state),result:'error',error:'save'});flush();
    assert.equal(root.querySelector('[data-tb-card="0"] [data-step="s0"]').getAttribute('aria-checked'),'false',
      'failed optimistic edits restore the authoritative state even when its data is unchanged');
    owner.receive(structuredClone(state));owner.dispose();
    assert.equal(frames.size,0,'dispose cancels a pending refresh');
  });
