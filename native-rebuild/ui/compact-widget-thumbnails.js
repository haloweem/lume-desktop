import {escapeHTML as h} from './dom.js';
import {t} from './localization.js';

const white='#f4f5f7',red='#e83332',orange='#f3a329',green='#35c978',blue='#3caafa',yellow='#f5cf30';
const svg=content=>`<svg viewBox="0 0 94 38" xmlns="http://www.w3.org/2000/svg" aria-hidden="true" focusable="false">${content}</svg>`;
const text=(value,x=47,y=23,size=13,color=white)=>
  `<text x="${x}" y="${y}" fill="${color}" font-family="Microsoft YaHei,Arial,sans-serif" font-size="${size}" font-weight="700" text-anchor="middle">${h(value)}</text>`;
const thermometer=(x=27,color=orange)=>`<g fill="none" stroke="${color}" stroke-width="2.4" stroke-linecap="round"><path d="M${x} 11v13a5 5 0 1 0 4 0V11a2 2 0 0 0-4 0Z"/><path d="M${x+2} 17v10"/></g>`;
const cloud=(x=22,color='#b9d8e9')=>`<path fill="${color}" d="M${x+4} 27h17a5 5 0 0 0 1-9 7 7 0 0 0-13-2 6 6 0 0 0-5 11Z"/>`;
const sun=(x=28,color='#ffd52f')=>`<g fill="${color}" stroke="${color}" stroke-width="1.7" stroke-linecap="round"><circle cx="${x}" cy="19" r="5"/><path d="M${x} 8v3m0 16v3m-11-11h3m16 0h3m-19-8 2 2m14 14 2 2m0-18-2 2m-14 14-2 2"/></g>`;
const drop=(x,y,color=blue)=>`<path fill="${color}" d="M${x} ${y-7}c-3 4-5 7-5 10a5 5 0 0 0 10 0c0-3-2-6-5-10Z"/>`;
const wind=()=>`<g fill="none" stroke="#42a8b5" stroke-width="2" stroke-linecap="round"><path d="M28 11v16m4-10v9m-9-7v7"/><circle cx="27" cy="27" r="3"/><circle cx="33" cy="25" r="2"/></g>`;
// System choices describe a function, independent of sampler availability.
const systemIcons={
  11:`<g fill="none" stroke-width="2.8" stroke-linecap="round" stroke-linejoin="round"><path stroke="${green}" d="M7 20V4m-4 4 4-4 4 4"/><path stroke="${blue}" d="M17 4v16m-4-4 4 4 4-4"/></g>`,
  12:`<rect x="2" y="4" width="20" height="14" rx="2.5" fill="${blue}"/><rect x="4.5" y="6.5" width="15" height="9" rx="1" fill="#153b56"/><path d="M10 18h4v3h-4z" fill="#8fcfff"/><path d="M8 21h8" stroke="#8fcfff" stroke-width="2" stroke-linecap="round"/><path d="M13 7l-4 5h3l-1 4 5-6h-3l1-3Z" fill="${yellow}"/>`,
  13:`<rect x="1" y="6" width="19" height="12" rx="3" fill="${green}"/><path d="M22 10v4" stroke="${green}" stroke-width="2.5" stroke-linecap="round"/><path d="M11 8l-4 5h4l-1 4 5-6h-4l1-3Z" fill="${white}"/>`,
  14:`<path d="M8 2v4m4-4v4m4-4v4M8 18v4m4-4v4m4-4v4M2 8h4m-4 4h4m-4 4h4M18 8h4m-4 4h4m-4 4h4" stroke="${orange}" stroke-width="2.2" stroke-linecap="round"/><rect x="5" y="5" width="14" height="14" rx="3" fill="${orange}"/><rect x="9" y="9" width="6" height="6" rx="1.2" fill="#ffe197"/>`,
  15:`<path d="M6 16v5m4-5v5m4-5v5m4-5v5" stroke="${yellow}" stroke-width="2.2"/><path d="M2 5h20v5a2 2 0 0 0 0 4v3H2v-3a2 2 0 0 0 0-4V5Z" fill="${green}"/><g fill="#175d3a"><rect x="5" y="8" width="3" height="6" rx=".7"/><rect x="10.5" y="8" width="3" height="6" rx=".7"/><rect x="16" y="8" width="3" height="6" rx=".7"/></g>`,
  16:`<path d="M3 13l3-9h12l3 9v5H3Z" fill="${blue}"/><path d="M8 7h8" stroke="#b2e4ff" stroke-width="2" stroke-linecap="round"/><rect x="3" y="13" width="18" height="8" rx="2.5" fill="#6074dc"/><path d="M7 17h5" stroke="#c6d4ff" stroke-width="2" stroke-linecap="round"/><circle cx="17" cy="17" r="1.4" fill="${green}"/>`,
  17:`<path d="M6 1v3m5-3v3M1 6h3m-3 5h3M6 13v3m7-10h3" stroke="${orange}" stroke-width="2" stroke-linecap="round"/><rect x="3" y="3" width="11" height="11" rx="2" fill="${orange}"/><rect x="6.5" y="6.5" width="4" height="4" rx=".8" fill="#ffe197"/><path d="M10 13h12v7H9v-6Z" fill="${green}" stroke="#000" stroke-width="1.5"/><path d="M12 20v2m4-2v2m4-2v2" stroke="${yellow}" stroke-width="1.8"/><path d="M13 15v3m5-3v3" stroke="#175d3a" stroke-width="2.5"/>`,
};
const temperatureIcon=color=>`<path d="M8 3a3 3 0 0 1 6 0v11a5 5 0 1 1-6 0V3Z" fill="${color}"/><path d="M11 4v13" stroke="#ffe3ac" stroke-width="2" stroke-linecap="round"/><circle cx="11" cy="18" r="2.4" fill="#ffe3ac"/>`;
const unavailableIcons={
  1:`<path d="M17 3a9.5 9.5 0 1 0 4 15A9 9 0 0 1 17 3Z" fill="${yellow}"/><path d="M7 2v4M5 4h4" stroke="#ffe9a5" stroke-width="1.6" stroke-linecap="round"/>`,
  19:`<circle cx="8" cy="8" r="4" fill="${yellow}"/><path d="M8 1v2M1 8h2m.5-4.5L5 5m9-1.5L12.5 5" stroke="${orange}" stroke-width="2" stroke-linecap="round"/><path d="M6 21h12a4 4 0 0 0 1-8 6 6 0 0 0-11-2 5 5 0 0 0-2 10Z" fill="#b9d8e9"/>`,
  20:temperatureIcon(orange)+`<path d="M19 5v6m-2-4 2-2 2 2M19 21v-6m-2 4 2 2 2-2" fill="none" stroke="${blue}" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>`,
  21:`<path d="M12 2C9 6 4 11 4 16a8 8 0 0 0 16 0c0-5-5-10-8-14Z" fill="${blue}"/><path d="M8 14c-1 3 0 5 3 6" fill="none" stroke="#b5e9ff" stroke-width="2" stroke-linecap="round"/>`,
  22:`<g fill="none" stroke-width="2.2" stroke-linecap="round"><path d="M2 8h12a3 3 0 1 0-3-3M2 12h17a3 3 0 1 1-3 3" stroke="#51d0d1"/><path d="M3 17h6a2.5 2.5 0 1 1-2.5 2.5" stroke="${blue}"/></g>`,
  23:`<circle cx="12" cy="12" r="10" fill="#3aadae"/><circle cx="12" cy="12" r="7" fill="#173f49"/><path d="M15 5 9 11l3 2Z" fill="${orange}"/><path d="m9 19 6-6-3-2Z" fill="#c9f5ef"/><circle cx="12" cy="12" r="1.5" fill="${white}"/>`,
  24:`<circle cx="12" cy="12" r="5" fill="${yellow}"/><path d="M12 1v3m0 16v3M1 12h3m16 0h3M4 4l2 2m12 12 2 2M20 4l-2 2M6 18l-2 2" stroke="${orange}" stroke-width="2" stroke-linecap="round"/>`,
  25:`<path d="M5 16a7 7 0 0 1 14 0Z" fill="${orange}"/><path d="M12 3v4M3 8l3 3m15-3-3 3" stroke="${yellow}" stroke-width="2" stroke-linecap="round"/><path d="M2 17h20M5 21h14" stroke="${blue}" stroke-width="2" stroke-linecap="round"/>`,
  26:`<path d="M4 20a10 10 0 1 1 16 0Z" fill="#9c83eb"/><path d="M7 17a6 6 0 1 1 10 0Z" fill="#372857"/><path d="m12 14 5-7" stroke="${orange}" stroke-width="2.2" stroke-linecap="round"/><circle cx="12" cy="14" r="2" fill="${white}"/>`,
  27:temperatureIcon(orange),
  28:`<path d="M1 12S5 5 12 5s11 7 11 7-4 7-11 7S1 12 1 12Z" fill="${blue}"/><circle cx="12" cy="12" r="4.5" fill="#c5efff"/><circle cx="12" cy="12" r="2.5" fill="#164f7d"/><circle cx="13" cy="11" r="1" fill="${white}"/>`,
  29:temperatureIcon('#ef7658')+`<circle cx="19" cy="6" r="3" fill="${yellow}"/><path d="M19 1v1m4 4h-1m-3 4v1" stroke="${orange}" stroke-width="1.6" stroke-linecap="round"/>`,
  30:`<path d="M5 15h14a4 4 0 0 0 0-8 6 6 0 0 0-11-2 5 5 0 0 0-3 10Z" fill="#b9d8e9"/><path d="m7 18-1 3m6-3-1 3m6-3-1 3" stroke="${blue}" stroke-width="2.4" stroke-linecap="round"/>`,
};
const thumbnailIcon=content=>svg(`<g transform="translate(35 7)">${content}</g>`);
const progress=(percent,color)=>{
  const start=31,width=34;
  return svg(text(`${percent}%`,47,19,14)+`<rect x="${start}" y="25" width="${width}" height="3" rx="1.5" fill="#747779"/>`+
    `<rect x="${start}" y="25" width="${width*percent/100}" height="3" rx="1.5" fill="${color}"/>`+
    `<circle cx="${start+width*percent/100}" cy="26.5" r="3" fill="${color}" stroke="#d9e4e3" stroke-width="1"/>`);
};

// Freeze these calendar values when the page opens; native readings replace
// them once available. Rendering a picker never starts a clock or sampler.
export function compactTimeReadings(capturedAt){
  const d=new Date(capturedAt),year=d.getFullYear(),month=d.getMonth(),day=d.getDate();
  if(!Number.isFinite(d.getTime()))return [];
  const fraction=(d.getHours()*3600+d.getMinutes()*60+d.getSeconds())/86400;
  const dayNumber=Date.UTC(year,month,day)/86400000;
  const firstQuarter=Math.floor(month/3)*3;
  const date=(y,m,n=1)=>Date.UTC(y,m,n)/86400000;
  const weekday=t(['周日','周一','周二','周三','周四','周五','周六'][d.getDay()],['Sun','Mon','Tue','Wed','Thu','Fri','Sat'][d.getDay()]);
  const values=[
    [2,`${weekday} ${String(day).padStart(2,'0')}`], [3,`${month+1}/${day}`], [4,weekday],
    [5,`${String(d.getHours()).padStart(2,'0')}:${String(d.getMinutes()).padStart(2,'0')}`],
    ...[[6,fraction],[7,((d.getDay()+6)%7+fraction)/7],
      [8,(day-1+fraction)/(date(year,month+1)-date(year,month))],
      [9,(dayNumber-date(year,firstQuarter)+fraction)/(date(year,firstQuarter+3)-date(year,firstQuarter))],
      [10,(dayNumber-date(year,0)+fraction)/(date(year+1,0)-date(year,0))]].map(([id,progress])=>[id,`${Math.round(progress*100)}%`,progress]),
  ];
  return values.map(([id,text,progress=null])=>({id,text,progress,secondary:'',available:true}));
}
export function compactWidgetThumbnail(id,reading){
  if(systemIcons[id])return thumbnailIcon(systemIcons[id]);
  if(id!==0&&(!reading||!reading.available))return unavailableIcons[id]?thumbnailIcon(unavailableIcons[id]):svg(text('—',47,23,14,'#a4a6a8'));
  const label=reading?.text||'';
  const percent=reading?.progress===null?null:Math.round((reading?.progress??0)*100);
  switch(id){
  case 0:return svg(text('—',47,24,17));
  case 1:{const parts=label.split('月');return parts.length===2?svg(text(`${parts[0]}/`,35,23,13)+text(parts[1],63,23,13,red)):svg(text(label,47,23,13));}
  case 2:case 4:return svg(text(label,47,23,13));
  case 3:{const parts=label.split('/');return svg(text(`${parts[0]}/`,41,23,14)+text(parts[1]||'',59,23,14,red));}
  case 5:{const [hours,minutes]=label.split(':').map(Number);const hand=(angle,length)=>`<path d="M47 19l${Math.sin(angle)*length} ${-Math.cos(angle)*length}" stroke-width="1.8"/>`;
    return svg(`<g stroke="${white}" stroke-linecap="round" fill="none"><circle cx="47" cy="19" r="2" fill="${white}" stroke="none"/><path d="M47 7v3m0 18v3M34 19h3m20 0h3" stroke-width="1.8"/>${hand((hours%12+minutes/60)*Math.PI/6,6)}${hand(minutes*Math.PI/30,10)}</g>`);}
  case 6:case 7:case 8:case 9:case 10:return progress(percent??0,[orange,green,blue,yellow,'#bd4bd8'][id-6]);
  case 19:return svg(cloud(20)+text(label,65,23,12));
  case 20:return svg(thermometer()+text(label,63,23,12));
  case 21:return svg(drop(28,19)+drop(37,24,'#55baff')+drop(20,25,'#55baff')+text(label,65,23,13));
  case 22:case 23:return svg(wind()+text(label,66,23,12));
  case 24:return svg(sun()+text(label,58,23,13));
  case 25:return svg(`<g fill="none" stroke="${orange}" stroke-width="2" stroke-linecap="round"><path d="M20 26h24m-20-4a8 8 0 0 1 16 0M32 7v5m-12 1 4 3m16-3-4 3"/></g>`+text(label,66,23,12));
  case 26:return svg(cloud(17,'#b9cde9')+text(label,65,23,11));
  case 27:case 29:return svg(thermometer()+text(label,61,23,13));
  case 28:return svg(cloud(21,'#6cc7e8')+`<path d="M23 29h20" stroke="#40a7d7" stroke-width="2"/>`+text(label,66,23,12));
  case 30:return svg(cloud(21,'#9ed9ef')+`<path d="M29 29v3m6-3v3m6-3v3" stroke="${blue}" stroke-width="2" stroke-linecap="round"/>`+text(label,66,23,13));
  default:return svg('');
  }
}
