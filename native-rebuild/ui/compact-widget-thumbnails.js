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
const ring=(percent=48,color=green,x=47,y=19,r=12)=>{
  const circumference=2*Math.PI*r;
  return `<circle cx="${x}" cy="${y}" r="${r}" fill="none" stroke="#343638" stroke-width="4"/>`+
    `<circle cx="${x}" cy="${y}" r="${r}" fill="none" stroke="${color}" stroke-width="4" stroke-linecap="round" stroke-dasharray="${circumference}" stroke-dashoffset="${circumference*(1-percent/100)}" transform="rotate(-90 ${x} ${y})"/>`;
};
const progress=(percent,color)=>{
  const start=31,width=34;
  return svg(text(`${percent}%`,47,19,14)+`<rect x="${start}" y="25" width="${width}" height="3" rx="1.5" fill="#747779"/>`+
    `<rect x="${start}" y="25" width="${width*percent/100}" height="3" rx="1.5" fill="${color}"/>`+
    `<circle cx="${start+width*percent/100}" cy="26.5" r="3" fill="${color}" stroke="#d9e4e3" stroke-width="1"/>`);
};

// Static examples copied from the reference picker. Live values belong only to the selected island widget.
export function compactWidgetThumbnail(id){
  switch(id){
  case 0:return svg(text('—',47,24,17));
  case 1:return svg(text(t('九 /','Sep /'),35,23,13)+text(t('廿五','25'),63,23,13,red));
  case 2:return svg(text(t('周二 03','Tue 03'),47,23,13));
  case 3:return svg(text('11/',41,23,14)+text('3',59,23,14,red));
  case 4:return svg(text(t('周二','Tue'),47,23,14));
  case 5:return svg(`<g stroke="${white}" stroke-linecap="round" fill="none"><circle cx="47" cy="19" r="2" fill="${white}" stroke="none"/><path d="M47 7v3m0 18v3M34 19h3m20 0h3M47 19l-3 6m3-6 3-5" stroke-width="1.8"/></g>`);
  case 6:return progress(56,orange);
  case 7:return progress(22,green);
  case 8:return progress(9,blue);
  case 9:return progress(36,yellow);
  case 10:return progress(84,'#bd4bd8');
  case 11:return svg(text('▲ 0 KB/s',48,16,9,green)+text('▼ 0 KB/s',48,28,9,'#59a7f0'));
  case 12:return svg(`<g fill="none" stroke="#9b9fa1" stroke-width="2.3" stroke-linejoin="round"><rect x="39" y="12" width="17" height="12" rx="1"/><path d="M47.5 24v4m-6 0h12"/></g>`);
  case 13:return svg(text('N/A',47,23,13,'#a4a6a8'));
  case 14:case 15:case 16:return svg(ring()+text('48%',47,22,9));
  case 17:return svg(ring(67,'#efc537',47,19,12)+ring(40,green,47,19,7));
  case 18:return svg(`<path fill="${white}" d="M30 13h18l4 5v10H26V18l4-5Zm1 2-3 6h22l-3-6h-5l-2 3h-4l-2-3h-3Z"/>`+text('3',64,24,14));
  case 19:return svg(cloud(20)+text(t('多云','Cloudy'),65,23,12));
  case 20:return svg(thermometer()+text('12°–20°',63,23,12));
  case 21:return svg(drop(28,19)+drop(37,24,'#55baff')+drop(20,25,'#55baff')+text('65%',65,23,13));
  case 22:return svg(wind()+text('3.6m/s',66,23,12));
  case 23:return svg(wind()+text(t('东风','E wind'),64,23,12));
  case 24:return svg(sun()+text('3',58,23,13));
  case 25:return svg(`<g fill="none" stroke="${orange}" stroke-width="2" stroke-linecap="round"><path d="M20 26h24m-20-4a8 8 0 0 1 16 0M32 7v5m-12 1 4 3m16-3-4 3"/></g>`+text('06:09',66,23,12));
  case 26:return svg(cloud(17,'#b9cde9')+text('1023hPa',65,23,11));
  case 27:return svg(thermometer()+text('20°',61,23,13));
  case 28:return svg(cloud(21,'#6cc7e8')+`<path d="M23 29h20" stroke="#40a7d7" stroke-width="2"/>`+text('21km',66,23,12));
  case 29:return svg(thermometer()+text('17°',61,23,13));
  case 30:return svg(cloud(21,'#9ed9ef')+`<path d="M29 29v3m6-3v3m6-3v3" stroke="${blue}" stroke-width="2" stroke-linecap="round"/>`+text('48%',66,23,13));
  default:return svg('');
  }
}
