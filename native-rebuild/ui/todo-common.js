// Helpers shared by the to-do board and the task sheet.
import {t} from './localization.js';

export const COLORS=['none','red','orange','yellow','green','blue'];
export const COLOR_RGB={none:'',red:'#ff6961',orange:'#ffa348',yellow:'#f5d65c',green:'#6bd89b',blue:'#65b4ff'};
export const FREQUENCIES=['none','daily','weekdays','weekly','monthly'];
export const dateRe=/^\d{4}-\d{2}-\d{2}$/,timeRe=/^(?:[01]\d|2[0-3]):[0-5]\d$/;

// --- dates -------------------------------------------------------------------
export const pad=value=>String(value).padStart(2,'0');
export const toKey=date=>`${date.getFullYear()}-${pad(date.getMonth()+1)}-${pad(date.getDate())}`;
export const fromKey=key=>{const [y,m,d]=key.split('-').map(Number);return new Date(y,m-1,d);};
export const addDays=(key,days)=>{const date=fromKey(key);date.setDate(date.getDate()+days);return toKey(date);};
export const dayDiff=(a,b)=>Math.round((fromKey(a)-fromKey(b))/864e5);
export const weekdays=()=>t('日一二三四五六','SMTWTFS').split('');
export const weekdayName=key=>t(['星期日','星期一','星期二','星期三','星期四','星期五','星期六'],['Sunday','Monday','Tuesday','Wednesday','Thursday','Friday','Saturday'])[fromKey(key).getDay()];
export function dayLabel(key,today){
  if(!key)return t('无日期','No date');
  const diff=dayDiff(key,today);
  if(diff===0)return t('今天','Today');
  if(diff===1)return t('明天','Tomorrow');
  if(diff===-1)return t('昨天','Yesterday');
  const date=fromKey(key);
  const sameYear=date.getFullYear()===fromKey(today).getFullYear();
  return t(`${sameYear?'':date.getFullYear()+'年'}${date.getMonth()+1}月${date.getDate()}日`,
    date.toLocaleDateString('en-US',sameYear?{month:'short',day:'numeric'}:{year:'numeric',month:'short',day:'numeric'}));
}
export function weekRange(today){
  const date=fromKey(today),offset=(date.getDay()+6)%7;
  const start=addDays(today,-offset);return [start,addDays(start,6)];
}

// --- icons -------------------------------------------------------------------
export const icon={
  plus:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M8 3v10M3 8h10" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"/></svg>',
  check:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m4 8.4 2.6 2.5L12 5.4" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  search:'<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="7" cy="7" r="4.6" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="m10.5 10.5 3 3" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>',
  trash:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3.5 4.5h9M6.5 4.5V3h3v1.5M5 4.5l.6 8.5h4.8l.6-8.5" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  more:'<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="3.5" cy="8" r="1.3" fill="currentColor"/><circle cx="8" cy="8" r="1.3" fill="currentColor"/><circle cx="12.5" cy="8" r="1.3" fill="currentColor"/></svg>',
  clock:'<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="5.6" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M8 5v3.2l2 1.3" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>',
  repeat:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3 7a4 4 0 0 1 6.8-2.8L11.5 6M13 9a4 4 0 0 1-6.8 2.8L4.5 10" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/><path d="M11.8 3.2v2.9H8.9M4.2 12.8V9.9h2.9" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  note:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4 4h8M4 7h8M4 10h5" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>',
  left:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m10 3.5-4.5 4.5 4.5 4.5" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  right:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="m6 3.5 4.5 4.5L6 12.5" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  sun:'<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="3" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M8 1.8v1.4M8 12.8v1.4M1.8 8h1.4M12.8 8h1.4M3.6 3.6l1 1M11.4 11.4l1 1M3.6 12.4l1-1M11.4 4.6l1-1" stroke="currentColor" stroke-width="1.4" stroke-linecap="round"/></svg>',
  chevron:'<svg class="tb-chevron" viewBox="0 0 16 16" aria-hidden="true"><path d="m4.5 6.5 3.5 3.5 3.5-3.5" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  gear:'<svg viewBox="0 0 16 16" aria-hidden="true"><circle cx="8" cy="8" r="2.2" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="8" cy="8" r="5.6" fill="none" stroke="currentColor" stroke-width="1.4" stroke-dasharray="2.2 1.6"/></svg>',
  steps:'<svg viewBox="0 0 16 16" aria-hidden="true"><rect x="3" y="2.5" width="10" height="11.5" rx="2.2" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M6 1.5h4M5.6 7.2l1.2 1.2 2.4-2.4M6 11h4" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  bell:'<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4.2 11V7.4a3.8 3.8 0 0 1 7.6 0V11l1 1.2H3.2zM6.6 13.6a1.5 1.5 0 0 0 2.8 0" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linejoin="round"/></svg>',
  empty:'<svg viewBox="0 0 48 48" aria-hidden="true"><rect x="8" y="10" width="32" height="30" rx="8" fill="none" stroke="currentColor" stroke-width="2"/><path d="M8 18h32M17 6v8M31 6v8" stroke="currentColor" stroke-width="2" stroke-linecap="round"/><path d="m19 29 4 4 7-8" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"/></svg>',
};
