// The task editor popup (todo-editor.html) opened by the calendar widget, the
// desktop to-do list and the island. It hosts the same sheet as the to-do board;
// the native window turns the sheet's commands into store commands.
import {setLanguage,t} from './localization.js';
import {draftOf,openTodoSheet} from './todo-sheet.js';
import {dateRe} from './todo-common.js';

const webview=globalThis.chrome?.webview;
const post=(action,fields={})=>{try{webview?.postMessage({version:1,scope:'todo-editor',action,...fields});}catch{}};
const container=document.querySelector('[data-tb-sheet]');
const errorText=code=>code==='invalid-time'?t('结束时间需要晚于开始时间。','The end must be after the start.'):
  code==='not-found'?t('这项任务已不存在。','This task no longer exists.'):
  code==='read-only'||code==='save'||code==='stale'?t('待办暂时无法保存，请稍后再试。','To-dos could not be saved right now.'):
  code==='limit'?t('子任务或待办数量已达上限。','The task or sub-step limit has been reached.'):t('请检查填写的内容。','Check the details you entered.');
let sheet=null;
function open(message){
  setLanguage(message.english?'en':'zh');
  document.documentElement.lang=message.english?'en':'zh-CN';
  document.title=message.english?'Lume task':'Lume 待办';
  const today=dateRe.test(message.today)?message.today:message.date;
  const task=message.task&&typeof message.task==='object'?message.task:null;
  sheet=openTodoSheet(container,{draft:draftOf(task,dateRe.test(message.date)?message.date:today),today,original:task,standalone:true,
    onSave:commands=>{sheet.setBusy(true);sheet.showError('');post('save',{commands});},
    onDelete:()=>{sheet.setBusy(true);post('delete');},
    onClose:()=>post('close'),
    onDrag:()=>post('drag')});
}
webview?.addEventListener('message',event=>{
  const message=event.data;
  if(!message||message.version!==1)return;
  if(message.type==='todo-editor-init'&&!sheet)open(message);
  else if(message.type==='todo-editor-result'&&sheet&&!message.ok){sheet.setBusy(false);sheet.showError(errorText(message.error));}
});
post('ready');
