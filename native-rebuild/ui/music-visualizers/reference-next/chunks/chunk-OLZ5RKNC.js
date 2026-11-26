import{a as w}from"./chunk-FSMYLL7F.js";import{c as P}from"./chunk-7SZRL6FY.js";var S=P(w(),1);var R=e=>e?.replace(/([a-z0-9])([A-Z])/g,"$1-$2").toLowerCase();function T(e,a,u=[]){if(a==null)throw new Error("[lucide]: iconNode is required when icon name is used");return{name:R(e),size:24,node:a,...u.length>0?{aliases:u}:{}}}var q=e=>{let a="",u=!1;for(let t of e){if(t==="-"||t==="_"||t<=" "){u=a.length>0;continue}a.length===0?a+=t.toLowerCase():a+=u?t.toUpperCase():t,u=!1}return a};var y=e=>{let a=q(e);return a.charAt(0).toUpperCase()+a.slice(1)};var I=P(w(),1);var m=(...e)=>e.filter((a,u,t)=>!!a&&a.trim()!==""&&t.indexOf(a)===u).join(" ").trim();var d={xmlns:"http://www.w3.org/2000/svg",width:24,height:24,viewBox:"0 0 24 24",fill:"none",stroke:"currentColor","stroke-width":2,"stroke-linecap":"round","stroke-linejoin":"round"};function A(e){return e!=null}function b(e,a={}){let u=a.attributeNames??{},t=o=>u[o]??o,s=e.size??e.width??d.width,n=e.size??e.height??d.height,p=e.aliases?.filter(o=>typeof o=="string"&&o.trim()!=="").map(o=>`lucide-${o}`)??[],L=[...e.name?[`lucide-${e.name}`]:[],...p],f=a.className?.split(" ").filter(Boolean)??[],x=a.includeDefaultClasses===!1?m(...f):m("lucide",...L,...f),g=a.absoluteStrokeWidth?Number(a.strokeWidth??d["stroke-width"])*Number(e.size??e.width??d.width)/Number(a.size??a.width??d.width):a.strokeWidth??d["stroke-width"];return["svg",{...Object.entries(d).reduce((o,[r,i])=>(o[t(r)]=i,o),{}),..."color"in a&&a.color&&{[t("stroke")]:a.color},..."size"in a&&A(a.size)&&{[t("width")]:a.size,[t("height")]:a.size},..."width"in a&&A(a.width)&&{[t("width")]:a.width},..."height"in a&&A(a.height)&&{[t("height")]:a.height},[t("stroke-width")]:g,...x&&{[t("class")]:x},[t("viewBox")]:`0 0 ${s} ${n}`,...a.hasA11yProp===!1?{[t("aria-hidden")]:"true"}:{},..."attributes"in a&&a.attributes},e.node.map(o=>{let[r,i,C]=o,h=a.nonScalingStroke?{[t("vector-effect")]:"non-scaling-stroke",...i}:i;return C?[r,h,C]:[r,h]})]}function U(e,a={}){return b(e,{...a,attributeNames:{...a.attributeNames,class:"className","stroke-width":"strokeWidth","stroke-linecap":"strokeLinecap","stroke-linejoin":"strokeLinejoin","vector-effect":"vectorEffect"}})}var O=e=>{for(let a in e)if(a.startsWith("aria-")||a==="role"||a==="title")return!0;return!1};var c=P(w(),1);var Y=(0,c.createContext)({});var H=()=>(0,c.useContext)(Y);var v=(0,I.forwardRef)(({color:e,size:a,width:u,height:t,strokeWidth:s,absoluteStrokeWidth:n,nonScalingStroke:p,className:L="",children:f,iconNode:x=[],icon:g={node:x,aliases:[],size:24},...k},o)=>{let{size:r=24,strokeWidth:i=2,absoluteStrokeWidth:C=!1,nonScalingStroke:h=!1,color:z="currentColor",className:X=""}=H()??{},N=!!f||O(k),[K,Z,Q=[]]=U(g,{color:e??z,width:u??a??r,height:t??a??r,strokeWidth:s??i,absoluteStrokeWidth:n??C,nonScalingStroke:p??h,className:m(X,L),hasA11yProp:N,attributes:k});return(0,I.createElement)(K,{ref:o,...Z},[...Q.map(([J,j])=>(0,I.createElement)(J,j)),...Array.isArray(f)?f:[f]])});function l(e,a=[],u=[]){let t=typeof e=="string"?T(e,a,u):e,s=(0,S.forwardRef)(({className:n,...p},L)=>(0,S.createElement)(v,{ref:L,icon:t,className:n,...p}));return t.name&&(s.displayName=y(t.name)),s}var G={name:"star",size:24,node:[["path",{d:"M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z",key:"r04s7s"}]]};G.node;var B=l(G);var W={name:"check",size:24,node:[["path",{d:"M20 6 9 17l-5-5",key:"1gmf2c"}]]};W.node;var D=l(W);var V={name:"rotate-ccw",size:24,node:[["path",{d:"M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8",key:"1357e3"}],["path",{d:"M3 3v5h5",key:"1xhq8a"}]]};V.node;var M=l(V);var E={name:"x",size:24,node:[["path",{d:"M18 6 6 18",key:"1bl5f8"}],["path",{d:"m6 6 12 12",key:"d8bk6v"}]]};E.node;var F=l(E);export{D as a,M as b,B as c,F as d};
/*! Bundled license information:

lucide-react/dist/esm/shared/src/utils/toKebabCase.mjs:
lucide-react/dist/esm/shared/src/utils/toLucideIconData.mjs:
lucide-react/dist/esm/shared/src/utils/toCamelCase.mjs:
lucide-react/dist/esm/shared/src/utils/toPascalCase.mjs:
lucide-react/dist/esm/shared/src/utils/mergeClasses.mjs:
lucide-react/dist/esm/shared/src/build/defaultAttributes.mjs:
lucide-react/dist/esm/shared/src/build/buildLucideIconNode.mjs:
lucide-react/dist/esm/shared/src/build/buildLucideIconForReact.mjs:
lucide-react/dist/esm/shared/src/utils/hasA11yProp.mjs:
lucide-react/dist/esm/context.mjs:
lucide-react/dist/esm/Icon.mjs:
lucide-react/dist/esm/createLucideIcon.mjs:
lucide-react/dist/esm/icons/star.mjs:
lucide-react/dist/esm/icons/check.mjs:
lucide-react/dist/esm/icons/rotate-ccw.mjs:
lucide-react/dist/esm/icons/x.mjs:
lucide-react/dist/esm/lucide-react.mjs:
  (**
   * @license lucide-react v1.48.0 - ISC
   *
   * This source code is licensed under the ISC license.
   * See the LICENSE file in the root directory of this source tree.
   *)
*/
