import {escapeHTML as h} from './dom.js';

const names=new Set(['home','music','todo','quick-launch','clock','notifications','settings']);
export function lumeIcon(name,{selected=false}={}) {
  if(name==='back'||name==='details')return `<img class="lume-ui-icon" src="assets/icons/lume-rounded-v1/${name}.svg" width="24" height="24" alt="" aria-hidden="true" draggable="false">`;
  if(!names.has(name))throw new RangeError(`Unknown Lume icon: ${name}`);
  const root=`assets/icons/lume-rounded-v1/${name}-${selected?'selected':'default'}`;
  return `<img class="lume-ui-icon" src="${h(root)}-24.png" srcset="${h(root)}-48.png 2x, ${h(root)}-72.png 3x" width="24" height="24" alt="" aria-hidden="true" draggable="false">`;
}
