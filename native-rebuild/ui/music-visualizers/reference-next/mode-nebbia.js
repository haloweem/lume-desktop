import{a as $e,b as xe,h as ye,i as ht,j as xn,k as mt,l as yn}from"./chunks/chunk-ZTTEMNJP.js";import{a as nn}from"./chunks/chunk-YRPFHNEH.js";import{a as mn}from"./chunks/chunk-TX4JZLSM.js";import{a as hr,d as mr,f as hn,i as pn,j as dn}from"./chunks/chunk-2RCK3FTM.js";import{a as br,b as gn,c as ft,d as pr,e as vn}from"./chunks/chunk-UPRBXN3G.js";import{a as on,b as an,c as sn,d as ln,e as un}from"./chunks/chunk-E7KZXXVV.js";import{a as je,b as Ye,d as ct,f as cn,g as fn,h as bn,i as ut,j as de,k as fr,m as W}from"./chunks/chunk-IBKL56ZZ.js";import"./chunks/chunk-EMT4AJ66.js";import{a as tn}from"./chunks/chunk-EEYXFQSP.js";import{a as rn}from"./chunks/chunk-DWAWEDYQ.js";import"./chunks/chunk-L6WC4GVU.js";import{a as eo,b as en}from"./chunks/chunk-FSMYLL7F.js";import"./chunks/chunk-NXLJLHUZ.js";import{c as ur}from"./chunks/chunk-7SZRL6FY.js";var H=ur(eo(),1);var wn="data-picture-arriving";var Ce="icon:",Ge=o=>o.startsWith(Ce),bt={"\u2764":"Heart","\u2665":"Heart","\u{1F497}":"Heart","\u2B50":"Star","\u2605":"Star","\u2606":"Star","\u{1F319}":"Moon","\u2600":"Sun","\u{1F31E}":"Sun","\u{1F3B5}":"Music","\u266A":"Music2","\u{1F3B6}":"Music","\u{1F338}":"Flower","\u273F":"Flower2","\u{1F337}":"Flower2","\u2728":"Sparkles","\u2601":"Cloud","\u{1F327}":"CloudRain","\u2602":"Umbrella","\u2614":"Umbrella","\u{1F431}":"Cat","\u{1F60A}":"Smile","\u{1F642}":"Smile","\u2744":"Snowflake","\u{1F343}":"Leaf","\u{1F33F}":"Leaf","\u{1FAB6}":"Feather"},En=["Heart","Star","Moon","Music","Flower","Sparkles","CloudRain","Umbrella","Smile","Leaf"],An=o=>{let e=new Set;for(let t of o)for(let r of t.fullText.replace(/[️‍]/g,""))bt[r]&&e.add(bt[r]);return[...e]},to=o=>{let e=o.replace(/[️‍]/g,"");return bt[e]?Ce+bt[e]:e},ro=(o,e,t)=>{let r=[],i=0;for(let s of e)r.push(i),i+=s.length;let n=s=>{let l=0;for(;l+1<r.length&&r[l+1]<=s;)l+=1;return l},a=[];for(let s of dn(o)){if(!s.segment.trim())continue;let l=n(s.index),c=n(s.index+s.segment.length-1),u=[];for(let h=l;h<=c;h+=1)t[h].ch.trim()&&(Ge(t[h].ch)?(u.length&&a.push(u),a.push([h]),u=[]):u.push(h));u.length&&a.push(u)}return a},Sn=(o,e,t,r)=>{let i=de(String(e));return o.map((n,a)=>{let s=pn(n),l=s.map(b=>({ch:to(b.char),t:b.startTime,d:Math.max(0,b.endTime-b.startTime)})),c=ro(n,s.map(b=>b.char),l),u=Math.max(n.startTime,ct(n)),h=o[a+1]?.startTime??1/0,m=W(i+Math.imul(a+1,977)+31>>>0),f=l[l.length-1];if(f&&r.length&&!l.some(b=>Ge(b.ch))&&m()<t){let b=f.t+f.d;l.push({ch:" ",t:b,d:0},{ch:Ce+r[Math.floor(m()*r.length)],t:b+.08,d:1.1}),c.push([l.length-1])}return{chars:l,words:c,t:n.startTime,end:u,next:h}})};var no=o=>{let e=typeof Intl<"u"&&"Segmenter"in Intl?new Intl.Segmenter(void 0,{granularity:"grapheme"}):null;return e?[...e.segment(o)].map(t=>t.segment):[...o]},io=(o,e)=>{let t=[],r=0;for(let s of e)t.push(r),r+=s.length;let i=[],n=typeof Intl<"u"&&"Segmenter"in Intl?new Intl.Segmenter(void 0,{granularity:"word"}):null,a=n?[...n.segment(o)].map(s=>({at:s.index,text:s.segment})):[...o.matchAll(/\S+/g)].map(s=>({at:s.index??0,text:s[0]}));for(let s of a){if(!s.text.trim())continue;let l=t.flatMap((c,u)=>c>=s.at&&c<s.at+s.text.length&&e[u].trim()?[u]:[]);l.length&&i.push(l)}return i},Mn=(o,e,t)=>{let r=no(o),i=r.map((a,s)=>({ch:a,t:e+s*.1,d:.5})),n=i[i.length-1];return{chars:i,words:io(o,r),t:e,end:n?n.t+n.d:e,next:1/0,credits:t}},Tn=(o,e,t)=>{let r=e?.trim();if(!o.length||!r)return o;let i=Math.max(...o.map(l=>l.end))+2.5,n=o.map((l,c)=>c===o.length-1?{...l,next:Math.min(l.next,i)}:l),a=Mn(r,i,"title");n.push(a);let s=t?.trim();return s&&n.push(Mn(s,a.end+.2,"artist")),n};var _={edge:.6,beads:.35,drip:.25,refract:1,glint:1,fieldDrops:.5,wind:0,endBead:1,endBeadMin:48,beadHold:4,breathSpread:1.2,breathHold:1,breathFade:8,breathThick:.7,bumpRate:1,bumpFall:.5},q=.5,Je=4,S=(o,e=0,t=1)=>Math.max(e,Math.min(t,o)),N=o=>o*o*(3-2*o);var ge=[1.5,3,5,8],oo=[11,8,5.5,3.6],ao=.18,so=.35,lo=3,Rn=2.5,co=6,Qe=1/480,pt=class{constructor(){this.u=ge.map(()=>[0,0]);this.v=ge.map(()=>[0,0]);this.last=-1}step(e,t,r){let i=this.last;if(this.last=e,t||i<0||e<=i)return;let n=Math.max(i,e-.1);for(;n<e;){let a=Math.min(Qe,e-n),s=r(n-Qe),l=r(n),c=r(n+Qe);for(let u=0;u<2;u+=1){let h=-lo*so*(c[u]-2*l[u]+s[u])/(Qe*Qe);ge.forEach((m,f)=>{let b=oo[f]*Math.PI*2,p=h-2*ao*b*this.v[f][u]-b*b*this.u[f][u];this.v[f][u]+=p*a,this.u[f][u]+=this.v[f][u]*a})}n+=a}}offset(e){let t=0;for(;t<ge.length-2&&e>ge[t+1];)t+=1;let r=S((e-ge[t])/(ge[t+1]-ge[t])),i=S((e-Rn)/(co-Rn)),n=(this.u[t][0]+(this.u[t+1][0]-this.u[t][0])*r)*i,a=(this.u[t][1]+(this.u[t+1][1]-this.u[t][1])*r)*i,s=S((Math.abs(a)-Math.abs(n)*.5)/Math.max(e,1)*.12,-.25,.35);return[n,a,s]}};var uo=.6,ho=.3,Ze=(o,e)=>-o*_.wind*e,dt=class{constructor(){this.drops=[];this.trails=[];this.smears=[];this.fieldDrops=[];this.bumps=[];this.nextBump=0;this.rng=W(777);this.now=0;this.H=0;this.lastTrigger=-1/0;this.spring=new pt;this.bump=1}buildField(e,t){let r=W(4242),i=Math.round(e*t/2200*_.fieldDrops);this.fieldDrops=[];for(let n=0;n<i;n+=1){let s=r()<.12?40+r()*34:14+r()**1.4*22,l=r()*.25,c=r()*e,u=t*(1-r()**1.25);this.fieldDrops.push({x:c,y:u,hx:c,hy:u,w:s*(1+l*.5),h:s*(1+l),phase:r()*6.28,fall:null,smear:null})}this.trails.length=0,this.smears.length=0,this.H=t}beginFrame(e,t,r=!0){this.now=e,this.drops.length=0,this.spring.step(e,t,i=>this.shakeAt(i)),!(t||!r||this.bump<=0||_.bumpRate<=0)&&e>this.nextBump&&(this.nextBump>0&&this.trigger(e,.5+this.rng()*.7),this.nextBump=e+(3+this.rng()*9)/_.bumpRate)}trigger(e,t){for(this.bumps.push({t:e,a:t});this.bumps.length&&e-this.bumps[0].t>3;)this.bumps.shift();let r=S((e-this.lastTrigger)/7.5);this.lastTrigger=e;for(let i of this.fieldDrops)if(!(i.fall||i.w<26)&&this.rng()<_.bumpFall*t*r*(i.w-24)/45){i.smear&&i.smear.end===null&&(i.smear.end=e),i.smear=null;let n={x:i.x,y0:i.y-i.h*.05,y1:i.y,w:i.w,end:null,seed:this.rng()*100};i.fall={t0:e+this.rng()*.12,y0:i.y,len:(30+this.rng()*150)*i.w/50,dur:1+this.rng()*2.2,trail:n},this.trails.push(n)}}shake(){return this.shakeAt(this.now)}shakeAt(e){let t=0,r=0;for(let i of this.bumps){let n=e-i.t;if(n<0)continue;let a=i.a*this.bump;r+=a*7*Math.exp(-n*4.5)*Math.sin(n*Math.PI*2*3.8),t+=a*2*Math.exp(-n*5.5)*Math.sin(n*Math.PI*2*2.7+1.2)*(1-Math.exp(-n*60))}return[t,r]}wobble(e){let t=0;for(let r of this.bumps){let i=this.now-r.t;i>=0&&(t+=r.a*this.bump*.07*Math.exp(-i*3.2)*Math.sin(i*Math.PI*2*7+e))}return t}put(e,t,r,i,n=1,a=1){if(r<.5||i<=.01)return;let[s,l,c]=this.spring.offset(r),u=this.wobble(e*.05+t*.03)-c,h=r*8*n*(1+u),m=r*8*a*(1-u);this.drops.push(e+s,t+l,h,m,h/100,Math.min(1,i))}fieldInstances(e,t){e.length=0;for(let r of this.fieldDrops){let i=r.x,n=r.y,a=0;if(r.fall){let h=r.fall,m=h.trail,f=S((this.now-h.t0)/h.dur);n=h.y0+h.len*(f-Math.sin(f*6*Math.PI)/(6*Math.PI)),i=m.x+Math.sin(n*.07+m.seed)*1.4+Math.sin(n*.021+m.seed*2)*2.2+Ze(n-m.y0,t),a=.4*(1-Math.cos(f*6*Math.PI))*(f<1?1:0),m.y1=n,f>=1&&(m.end=this.now,r.fall=null,n-r.h/8>this.H?(r.x=r.hx,r.y=r.hy):(r.x=i,r.y=n))}let[s,l,c]=this.spring.offset(r.w/8);r.fall||this.trackSmear(r,s,l);let u=this.wobble(r.phase)-c;e.push(i+s,n+l,r.w*(1+u),r.h*(1-u+a),r.w/100,1)}return e}trackSmear(e,t,r){let i=Math.hypot(t,r),n=e.smear;if(i<ho){n&&n.end===null&&(n.end=this.now);return}if(n?.gone&&(n=null),!n){if(i<uo)return;n={x:e.x,y:e.y,w:e.w,ax:0,ay:0,bx:0,by:0,end:null},e.smear=n,this.smears.push(n)}n.end=null,r<n.ay&&(n.ax=t,n.ay=r),r>n.by&&(n.bx=t,n.by=r)}};var Fn="./assets/nebbiaRaindrop-QVKH4ZYL.png";var Ae=(o,e,t)=>{let r=o.createShader(e);return o.shaderSource(r,t),o.compileShader(r),r},be=(o,e,t,r)=>{let i=o.createProgram();o.attachShader(i,e),o.attachShader(i,t);for(let[n,a]of Object.entries(r))o.bindAttribLocation(i,a,n);return o.linkProgram(i),i},Ln=(o,e)=>{if(o.getProgramParameter(e,o.LINK_STATUS))return;let t=(o.getAttachedShaders(e)??[]).map(r=>o.getShaderInfoLog(r)).filter(Boolean).join(`
`);throw new Error(`Nebbia program: ${o.getProgramInfoLog(e)}${t?`
${t}`:""}`)},J=(o,e)=>{let t=o.createTexture();return o.activeTexture(o.TEXTURE0+e),o.bindTexture(o.TEXTURE_2D,t),o.texParameteri(o.TEXTURE_2D,o.TEXTURE_MIN_FILTER,o.LINEAR),o.texParameteri(o.TEXTURE_2D,o.TEXTURE_MAG_FILTER,o.LINEAR),o.texParameteri(o.TEXTURE_2D,o.TEXTURE_WRAP_S,o.CLAMP_TO_EDGE),o.texParameteri(o.TEXTURE_2D,o.TEXTURE_WRAP_T,o.CLAMP_TO_EDGE),{t,unit:e}};var et=[{b:.0012,y0:.03,y1:.63,fog:!1},{b:.03,y0:0,y1:.66,fog:!0},{b:.09,y0:-.06,y1:.73,fog:!0}],kn=[.02,.035],fo=.5,dr=.15,xr=.2,bo=(o,e,t,r)=>{let i=Math.floor(o/t*r),n=Math.min(r,Math.ceil(e/t*r)-i),a=(i%r+r)%r,s=Math.min(Math.max(n,0),r-a),l=n<=0?[]:n>s?[[a,s],[0,n-s]]:[[a,n]];return{x0:i*t/r,runs:l}},Bn=["uFillB","uFillLayer","uFillX0","uFillGeo","uFillSize"],xt=class{constructor(e,t){this.gl=e;this.caches=[];this.span=1;this.aspect=1;et.forEach((r,i)=>kn.forEach((n,a)=>{let s=J(e,t+i*2+a);e.texParameteri(e.TEXTURE_2D,e.TEXTURE_WRAP_S,e.REPEAT),this.caches.push({tex:s,fb:e.createFramebuffer(),level:i,layer:a,w:1,h:1,lo:0,hi:0,valid:!1})}))}bindSamplers(e){let{gl:t}=this;t.useProgram(e),this.caches.forEach((r,i)=>t.uniform1i(t.getUniformLocation(e,`uBld${i}`),r.tex.unit))}invalidate(){for(let e of this.caches)e.valid=!1}resize(e,t,r){let{gl:i}=this;this.aspect=e/t,this.span=this.aspect+2*dr+xr;let n=i.getParameter(i.MAX_TEXTURE_SIZE);for(let a of this.caches){let s=et[a.level],l=Math.min(s.fog?r:t*fo,n/this.span);a.w=Math.max(1,Math.ceil(this.span*l)),a.h=Math.max(1,Math.ceil((s.y1-s.y0)*l)),i.activeTexture(i.TEXTURE0+a.tex.unit),i.bindTexture(i.TEXTURE_2D,a.tex.t),i.texImage2D(i.TEXTURE_2D,0,i.RGBA8,a.w,a.h,0,i.RGBA,i.UNSIGNED_BYTE,null),i.bindFramebuffer(i.FRAMEBUFFER,a.fb),i.framebufferTexture2D(i.FRAMEBUFFER,i.COLOR_ATTACHMENT0,i.TEXTURE_2D,a.tex.t,0),a.valid=!1}i.bindFramebuffer(i.FRAMEBUFFER,null)}update(e,t,r){let{gl:i}=this,n=-1,a=!1;for(let l of this.caches){let c=e*kn[l.layer],u=c-dr,h=c+this.aspect+dr;if(l.valid&&u>=l.lo&&h<=l.hi)continue;let m=l.valid&&u>=l.lo&&h-l.hi<this.span/2;if(!m&&n>=0&&n!==l.level){l.valid=!1;continue}if(a||(i.useProgram(t),i.enable(i.SCISSOR_TEST),a=!0),m){let f=h+xr/4;this.fill(l,l.hi,f,r),l.hi=f,l.lo=f-this.span}else n=l.level,l.lo=u-xr/4,l.hi=l.lo+this.span,this.fill(l,l.lo,l.hi,r),l.valid=!0}a&&(i.disable(i.SCISSOR_TEST),i.bindFramebuffer(i.FRAMEBUFFER,null));let s=et.map((l,c)=>this.caches[c*2].valid&&this.caches[c*2+1].valid?1:0);return{on:[s[0],s[1],s[2]],geo:et.flatMap(l=>[l.y0,l.y1,this.span])}}fill(e,t,r,i){let{gl:n}=this,a=et[e.level],{x0:s,runs:l}=bo(t,r,this.span,e.w);if(l.length){n.activeTexture(n.TEXTURE0+e.tex.unit),n.bindTexture(n.TEXTURE_2D,null),n.bindFramebuffer(n.FRAMEBUFFER,e.fb),n.viewport(0,0,e.w,e.h),n.uniform1f(i.uFillB,a.b),n.uniform1f(i.uFillLayer,e.layer),n.uniform1f(i.uFillX0,s),n.uniform3f(i.uFillGeo,a.y0,a.y1,this.span),n.uniform2f(i.uFillSize,e.w,e.h);for(let[c,u]of l)n.scissor(c,0,u,e.h),n.drawArrays(n.TRIANGLE_STRIP,0,4);n.bindTexture(n.TEXTURE_2D,e.tex.t)}}};var po=4,yt=class{constructor(e,t,r,i=null){this.gl=e;this.ok=r;this.layers=[{tex:J(e,t),fb:r?e.createFramebuffer():null,div:Je,b:.03,w:1,h:1},{tex:J(e,t+1),fb:r?e.createFramebuffer():null,div:Je*2,b:.09,w:1,h:1}],r&&i!==null&&this.layers.push({tex:J(e,i),fb:e.createFramebuffer(),div:po,b:.006,w:1,h:1})}resize(e,t){let{gl:r}=this;if(this.ok){for(let i of this.layers)i.w=Math.max(1,Math.ceil(e/i.div)),i.h=Math.max(1,Math.ceil(t/i.div)),r.activeTexture(r.TEXTURE0+i.tex.unit),r.bindTexture(r.TEXTURE_2D,i.tex.t),r.texImage2D(r.TEXTURE_2D,0,r.RGBA16F,i.w,i.h,0,r.RGBA,r.HALF_FLOAT,null),r.bindFramebuffer(r.FRAMEBUFFER,i.fb),r.framebufferTexture2D(r.FRAMEBUFFER,r.COLOR_ATTACHMENT0,r.TEXTURE_2D,i.tex.t,0);r.bindFramebuffer(r.FRAMEBUFFER,null)}}draw(e){let{gl:t}=this;if(this.ok){for(let r of this.layers)t.bindFramebuffer(t.FRAMEBUFFER,r.fb),t.viewport(0,0,r.w,r.h),t.uniform1f(e,r.b),t.drawArrays(t.TRIANGLE_STRIP,0,4);t.bindFramebuffer(t.FRAMEBUFFER,null)}}};var gt=class{constructor(e,t){this.gl=e;this.w=1;this.h=1;this.ok=!1;this.dirty=!1;this.tex=J(e,t),this.fb=e.createFramebuffer()}resize(e,t){let{gl:r}=this,i=r.getParameter(r.MAX_TEXTURE_SIZE);this.w=e,this.h=t,this.ok=e<=i&&t<=i,this.ok&&(r.activeTexture(r.TEXTURE0+this.tex.unit),r.bindTexture(r.TEXTURE_2D,this.tex.t),r.texImage2D(r.TEXTURE_2D,0,r.RGBA8,e,t,0,r.RGBA,r.UNSIGNED_BYTE,null),r.bindFramebuffer(r.FRAMEBUFFER,this.fb),r.framebufferTexture2D(r.FRAMEBUFFER,r.COLOR_ATTACHMENT0,r.TEXTURE_2D,this.tex.t,0),r.bindFramebuffer(r.FRAMEBUFFER,null),this.dirty=!0)}update(e){let{gl:t}=this;return this.ok&&this.dirty&&(t.activeTexture(t.TEXTURE0+this.tex.unit),t.bindTexture(t.TEXTURE_2D,null),t.useProgram(e),t.bindFramebuffer(t.FRAMEBUFFER,this.fb),t.viewport(0,0,this.w,this.h),t.drawArrays(t.TRIANGLE_STRIP,0,4),t.bindFramebuffer(t.FRAMEBUFFER,null),t.bindTexture(t.TEXTURE_2D,this.tex.t),this.dirty=!1),this.ok}};var Se=(o,e,t,r)=>{let i=t[r],n=t[r+1],a=t[r+2]-i,s=t[r+3]-n,l=a*a+s*s,c=l>0?Math.max(0,Math.min(1,((o-i)*a+(e-n)*s)/l)):0;return Math.hypot(o-i-a*c,e-n-s*c)},vt=class{constructor(){this.data=new Float32Array(6*4096);this.count=0;this.a=1;this.b=0;this.c=0;this.d=1;this.e=0;this.f=0;this.k=1;this.path=[]}clear(){this.count=0,this.path.length=0}setTransform(e,t,r,i,n,a){this.a=e,this.b=t,this.c=r,this.d=i,this.e=n,this.f=a,this.k=Math.hypot(e,t)}moveTo(e,t){this.path.length&&this.path.push(NaN,NaN),this.lineTo(e,t)}lineTo(e,t){this.path.push(this.a*e+this.c*t+this.e,this.b*e+this.d*t+this.f)}stroke(e,t){let r=this.path,i=e*this.k;for(let n=2;n<r.length;n+=2)Number.isNaN(r[n])||Number.isNaN(r[n-2])||this.push(r[n-2],r[n-1],r[n],r[n+1],i,t);r.length=0}push(e,t,r,i,n,a){if((this.count+1)*6>this.data.length){let c=new Float32Array(this.data.length*2);c.set(this.data),this.data=c}let s=this.count*6,l=this.data;l[s]=e,l[s+1]=t,l[s+2]=r,l[s+3]=i,l[s+4]=n,l[s+5]=a,this.count+=1}};var In=`#version 300 es
in vec2 aCorner; in vec4 aSeg; in vec2 aParam;
uniform vec2 uView;
out vec2 vP; flat out vec4 vSeg; flat out vec2 vParam;
void main() {
  vec2 a = aSeg.xy, b = aSeg.zw, d = b - a;
  float l = length(d);
  vec2 t = l > 1e-5 ? d / l : vec2(1.0, 0.0), n = vec2(-t.y, t.x);
  // \u6700\u5BBD\u90A3\u5C42\u7684\u534A\u5BBD\u518D\u7559 1 \u50CF\u7D20\u6297\u952F\u9F7F
  float R = aParam.x * 0.65 + 1.0;
  vec2 p = (a + b) * 0.5 + t * aCorner.x * (l * 0.5 + R) + n * aCorner.y * R;
  vP = p; vSeg = aSeg; vParam = aParam;
  gl_Position = vec4(p / uView * 2.0 - 1.0, 0.0, 1.0);
}`,Nn=`#version 300 es
precision highp float;
in vec2 vP; flat in vec4 vSeg; flat in vec2 vParam;
out vec4 o;
// \u534A\u5BBD r \u7684\u7EBF\u79BB\u50CF\u7D20\u4E2D\u5FC3 d \u65F6\u7684\u8986\u76D6\u5EA6\uFF081 \u50CF\u7D20\u7EBF\u6027\u8FC7\u6E21\uFF0C\u63A5\u8FD1\u753B\u5E03\u7684\u6297\u952F\u9F7F\uFF09
float cover(float r, float d) { return clamp(r + 0.5 - d, 0.0, 1.0); }
// \u753B\u5E03\u7684\u989C\u8272\u662F 8 \u4F4D\uFF1A\u5148\u91CF\u5316\u518D\u4E58\u8986\u76D6\u5EA6
float level(float x) { return floor(x * 255.0 + 0.5) / 255.0; }
void main() {
  vec2 a = vSeg.xy, ba = vSeg.zw - a, pa = vP - a;
  float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
  float d = length(pa - ba * h), w = vParam.x * 0.5, v = vParam.y;
  // \u4E09\u5C42\u6309\u753B\u5E03\u7684 lighten \u4E00\u5C42\u5C42\u53E0\uFF1A\u6BCF\u5C42\u90FD\u6BD4\u4E0B\u9762\u4EAE\uFF0C\u8986\u76D6\u5EA6 a \u7684\u50CF\u7D20\u662F m + a \xD7 (\u8FD9\u5C42 \u2212 m)
  float m = level(v * 0.38) * cover(w * 1.3, d);
  m += cover(w, d) * (level(v * 0.72) - m);
  m += cover(w * 0.62, d) * (level(v) - m);
  o = vec4(m, 0.0, 0.0, 1.0);
}`,wt=class{constructor(e,t){this.gl=e;this.prog=null;this.uView=null;this.w=1;this.h=1;this.tex=J(e,t),this.fb=e.createFramebuffer(),this.vao=e.createVertexArray(),e.bindVertexArray(this.vao),e.bindBuffer(e.ARRAY_BUFFER,e.createBuffer()),e.bufferData(e.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),e.STATIC_DRAW),e.enableVertexAttribArray(0),e.vertexAttribPointer(0,2,e.FLOAT,!1,0,0),this.buf=e.createBuffer(),e.bindBuffer(e.ARRAY_BUFFER,this.buf);let r=24;e.enableVertexAttribArray(1),e.vertexAttribPointer(1,4,e.FLOAT,!1,r,0),e.vertexAttribDivisor(1,1),e.enableVertexAttribArray(2),e.vertexAttribPointer(2,2,e.FLOAT,!1,r,16),e.vertexAttribDivisor(2,1),e.bindVertexArray(null)}attach(e){this.prog=e,this.uView=this.gl.getUniformLocation(e,"uView")}resize(e,t){let{gl:r}=this;this.w=Math.max(1,Math.round(e*q)),this.h=Math.max(1,Math.round(t*q)),r.activeTexture(r.TEXTURE0+this.tex.unit),r.bindTexture(r.TEXTURE_2D,this.tex.t),r.texImage2D(r.TEXTURE_2D,0,r.R8,this.w,this.h,0,r.RED,r.UNSIGNED_BYTE,null),r.bindFramebuffer(r.FRAMEBUFFER,this.fb),r.framebufferTexture2D(r.FRAMEBUFFER,r.COLOR_ATTACHMENT0,r.TEXTURE_2D,this.tex.t,0),r.bindFramebuffer(r.FRAMEBUFFER,null)}draw(e){let{gl:t}=this;t.bindFramebuffer(t.FRAMEBUFFER,this.fb),t.viewport(0,0,this.w,this.h),t.clearColor(0,0,0,0),t.clear(t.COLOR_BUFFER_BIT),e.count&&this.prog&&(t.useProgram(this.prog),t.bindVertexArray(this.vao),t.bindBuffer(t.ARRAY_BUFFER,this.buf),t.bufferData(t.ARRAY_BUFFER,e.data.subarray(0,e.count*6),t.DYNAMIC_DRAW),t.uniform2f(this.uView,this.w,this.h),t.enable(t.BLEND),t.blendEquation(t.MAX),t.drawArraysInstanced(t.TRIANGLE_STRIP,0,4,e.count),t.blendEquation(t.FUNC_ADD),t.disable(t.BLEND),t.bindVertexArray(null)),t.bindFramebuffer(t.FRAMEBUFFER,null)}};var Pn=`
uniform float uBubbles;
// \u8DDF\u62CD\u5192\u7684\u6BCF\u6392\uFF1Ax\u3001\u5192\u51FA\u7684\u65F6\u523B\u3001\u5927\u5C0F\uFF080 \u662F\u7A7A\u683C\uFF09\u3001\u79CD\u5B50
uniform vec4 uBurst[12];
uniform int uBurstN;
// 1 \u65F6\u821E\u6F6E\u8E0F\u62CD\u7684\u6C14\u6CE1\u4E00\u9879\u5F00\u7740\uFF1A\u4E0D\u5192\u968F\u673A\u7684\u5927\u6CE1
uniform float uBeatRows;
// \u4E00\u9897\u6CE1\uFF08d \u662F\u5230\u6CE1\u5FC3\u7684\u504F\u79FB\uFF0Cy \u671D\u4E0B\uFF09\uFF1Argb \u52A0\u4E0A\u53BB\u7684\u5149\uFF0Ca \u6CE1\u91CC\u538B\u6697\u591A\u5C11
vec4 aqBubble(vec2 d, float rad, float b) {
  float l = length(d), aa = max(b, 0.0005);
  float keep = rad * rad / (rad * rad + b * b * 4.0);
  float disc = smoothstep(rad + aa, rad - aa, l);
  float up = 0.5 + 0.5 * clamp(-d.y / max(l, 1e-5), -1.0, 1.0);
  float fres = pow(clamp(l / rad, 0.0, 1.0), 5.0) * disc * (0.35 + 0.65 * up);
  float spec = glowPt(d + vec2(rad * 0.36, rad * 0.4), rad * 0.15, b, 0.0);
  float spec2 = glowPt(d - vec2(rad * 0.32, rad * 0.42), rad * 0.1, b, 0.0) * 0.3;
  float caus = glowPt(d - vec2(0.0, rad * 0.5), rad * 0.32, b, 0.0) * 0.14;
  vec4 big = vec4(vec3(fres * 0.9 * keep + spec * 1.3 + spec2 + caus), disc * 0.14 * keep);
  vec4 tiny = vec4(vec3(glowPt(d, rad * 0.6, b, 0.0) * 1.3 + spec * 1.2), 0.0);
  return mix(tiny, big, smoothstep(0.0018, 0.0034, rad));
}
vec3 aqBubbles(vec3 col, vec2 q, float b, vec3 bc) {
  float t = uTime;
  float drift = sceneryOn(SCN_RAIN_SLANT) ? 0.1 : 0.0;
  float fade = smoothstep(0.93, 0.8, q.y);
  float qx = q.x + (0.92 - q.y) * drift;
  // \u7EC6\u6CE1\uFF1A\u6BCF\u80A1\u4ECE\u4E00\u4E2A\u6CC9\u773C\u5192\u51FA\uFF0C\u8D8A\u5F80\u4E0A\u8D8A\u6563\u3001\u6CE1\u8D8A\u5927\u4E00\u70B9
  if (sceneryOn(SCN_RAIN)) {
    float cw = 0.055, cx = floor(qx / cw);
    for (float k = -1.0; k <= 1.0; k++) {
      float n = cx + k;
      if (h1(n * 2.3 + 1.0) > uBubbles * 0.9) continue;
      float src = (n + 0.5) * cw + (h1(n + 3.0) - 0.5) * cw * 0.5;
      float sp = 0.14 + 0.1 * h1(n + 7.0), ch = 0.014 + 0.01 * h1(n + 5.0);
      float rise = clamp((0.92 - q.y) / 0.9, 0.0, 1.0);
      float sw = 0.003 + 0.016 * rise, swe = sqrt(sw * sw + b * b);
      float sx = qx - src;
      col += bc * exp(-sx * sx / (swe * swe)) * 0.03 * fade * (sw / swe + 0.3);
      if (b > 0.02) continue;
      float v = q.y + t * sp, iy = floor(v / ch);
      for (float j = -1.0; j <= 1.0; j++) {
        float m = iy + j, hb = h1(m * 1.31 + n * 17.0);
        if (hb > 0.85) continue;
        // \u6BCF\u9897\u5728\u683C\u5B50\u91CC\u4E0A\u4E0B\u9519\u4E00\u70B9\u3001\u5DE6\u53F3\u6563\u5F00\uFF0C\u4E0D\u6392\u6210\u4E00\u4E32\u73E0\u5B50
        float y = (m + 0.2 + 0.6 * h1(m * 2.7 + n)) * ch - t * sp;
        float up = clamp((0.92 - y) / 0.9, 0.0, 1.0);
        float rad = 0.0012 + 0.003 * hb * hb * hb + 0.0012 * up;
        vec2 c = vec2(src + sin(m * 2.1 + t * 6.0) * (0.0015 + 0.006 * up) + (h1(m + n) - 0.5) * (0.006 + 0.03 * up), y);
        vec4 bub = aqBubble(vec2(qx, q.y) - c, rad, b);
        col = col * (1.0 - bub.a * fade) + bc * bub.rgb * fade * (0.6 + 0.4 * h1(m * 5.0 + n));
      }
    }
  }
  // \u5927\u6CE1\uFF1A\u5C11\u3001\u6162\uFF0C\u4E0A\u4E0B\u7565\u6241\uFF0C\u8FB9\u7F18\u4E00\u8FB9\u6D6E\u4E00\u8FB9\u8F7B\u8F7B\u6643
  if (sceneryOn(SCN_CARS) && uBeatRows < 0.5) {
    float bw = 0.13, bx = floor(qx / bw);
    for (float k = -1.0; k <= 1.0; k++) {
      float n = bx + k;
      float sp = 0.07 + 0.04 * h1(n + 41.0), ch = 0.42;
      float iy = floor((q.y + t * sp) / ch);
      for (float j = -1.0; j <= 0.0; j++) {
        float m = iy + j;
        if (h1(m * 3.7 + n * 11.0) > min(1.1 * uBubbles, 0.85)) continue;
        float rad = 0.008 + 0.016 * pow(h1(m + n * 5.0), 1.5);
        vec2 c = vec2((n + 0.2 + 0.6 * h1(m * 7.0 + n)) * bw + sin(t * 1.3 + m) * 0.02, (m + 0.5) * ch - t * sp);
        vec2 d = vec2(qx, q.y) - c;
        // \u6309\u65B9\u5411\u7F29\u653E\u504F\u79FB\uFF0C\u8F6E\u5ED3\u5C31\u6643\uFF1Asin(3\u03B8 + \u03C6)\u3001sin(2\u03B8 - \u03C6') \u7528\u65B9\u5411\u7684\u4F59\u5F26\u6B63\u5F26\u5C55\u5F00\uFF0C\u4E0D\u7528 atan
        vec2 u = d / max(length(d), 1e-5);
        float c2 = u.x * u.x - u.y * u.y, s2 = 2.0 * u.x * u.y;
        float c3 = u.x * (4.0 * u.x * u.x - 3.0), s3 = u.y * (3.0 - 4.0 * u.y * u.y);
        float f1 = t * 4.0 + m, f2 = t * 3.0;
        float shape = 1.0 + 0.03 * (s3 * cos(f1) + c3 * sin(f1)) + 0.02 * (s2 * cos(f2) - c2 * sin(f2));
        vec4 bub = aqBubble(vec2(d.x, d.y * 1.18) / shape, rad, b);
        col = col * (1.0 - bub.a * fade) + bc * (bub.rgb + glowPt(d, rad * 1.4, b, 0.0) * 0.04) * fade;
      }
    }
  }
  // \u8DDF\u62CD\u5192\u7684\u4E00\u6392\u6392\u5927\u6CE1\uFF1A\u4E00\u6392\u516D\u9897\uFF0C\u5192\u51FA\u65F6\u4ECE\u5C0F\u6DA8\u5230\u5168\u5927\uFF0C\u5148\u51B2\u4E00\u4E0B\u518D\u6162\u6162\u6D6E\uFF1B\u79BB\u8FD9\u4E00\u6392\u8FDC\u7684\u50CF\u7D20\u76F4\u63A5\u8DF3\u8FC7\u3002
  // \u6BCF\u9897\u5E26\u4E00\u5708\u5149\u6655\uFF0C\u96FE\u540E\u9762\u7CCA\u5F00\u4E5F\u662F\u4E00\u6392\u4EAE\u70B9\uFF0C\u770B\u5F97\u51FA\u8DDF\u62CD
  for (int i = 0; i < 12; i++) {
    if (i >= uBurstN) break;
    vec4 B = uBurst[i];
    float a = t - B.y;
    if (B.z <= 0.0 || a < 0.0) continue;
    float y0 = 0.9 - 0.2 * a - 0.28 * (1.0 - exp(-a * 3.0));
    if (abs(q.y - y0) > 0.08 + b * 3.0) continue;
    float grow = smoothstep(0.0, 0.15, a) * (0.55 + 0.45 * B.z);
    for (float k = 0.0; k < 6.0; k++) {
      float h = h1(B.w + k * 7.3), h2 = h1(B.w * 1.7 + k * 3.1);
      float rad = (0.008 + 0.012 * h) * grow;
      vec2 c = vec2(B.x + (k - 2.5) * 0.05 + (h2 - 0.5) * 0.02 + sin(a * 3.0 + k * 1.7) * 0.004 * a, y0 + (h - 0.5) * 0.04);
      vec2 d = vec2(qx, q.y) - c;
      vec4 bub = aqBubble(vec2(d.x, d.y * 1.18), rad, b);
      col = col * (1.0 - bub.a * fade) + bc * (bub.rgb + glowPt(d, rad * 2.0 + 0.004, b, 0.0) * 0.22) * fade;
    }
  }
  return col;
}
`;var Dn=`
// \u6BCF\u53EA\uFF1Axy \u4F4D\u7F6E\uFF08scene \u5750\u6807\uFF09\u3001z \u4F1E\u76D6\u534A\u5F84\u3001w \u6536\u7F29\uFF080 \u8212\u5F20\uFF0C1 \u6536\u5F97\u6700\u7D27\uFF09\uFF1B\u989C\u8272 rgb\uFF08\u5DF2\u6309\u8FDC\u8FD1\u8C03\u6697\uFF09\uFF0Ca \u89E6\u624B\u6CE2\u7684\u76F8\u4F4D
uniform vec4 uJelly[24], uJellyC[24];
uniform int uJellyN;
vec3 jellies(vec2 q, float b) {
  vec3 c = vec3(0.0);
  float sharp = 1.0 - clamp(b * 25.0, 0.0, 1.0);
  bool fine = b < 0.004;
  for (int i = 0; i < 24; i++) {
    if (i >= uJellyN) break;
    vec4 J = uJelly[i];
    float size = J.z, pulse = J.w;
    vec2 d = q - J.xy;
    if (abs(d.x) > size * 2.2 + b * 3.0 || d.y < -size * 1.6 - b * 3.0 || d.y > size * 5.0 + b * 3.0) continue;
    vec3 jc = uJellyC[i].rgb * uNeon;
    float ph = uJellyC[i].a;
    // \u4F1E\u76D6\uFF1A\u6536\u7F29\u65F6\u53D8\u7A84\u53D8\u9AD8\uFF1B\u4E0B\u6CBF\u5F80\u4E24\u8FB9\u5782\u3001\u5E26\u4E00\u5708\u6CE2\u7EB9
    float wx = size * (1.0 - 0.18 * pulse), hy = size * 0.72 * (1.0 + 0.14 * pulse);
    vec2 p = vec2(d.x / wx, d.y / hy);
    float soft = b / size + 0.04;
    // \u4E0B\u6CBF\u90A3\u4E00\u6BB5 r \u6DA8\u5F97\u5FEB\uFF08\xF70.3\uFF09\uFF0C\u6A21\u7CCA\u65F6\u6309 soft \u653E\u7F13\uFF0C\u4E0D\u7136\u96FE\u540E\u9762\u4F1E\u88D9\u4E0B\u6CBF\u8FD8\u662F\u4E00\u9053\u786C\u8FB9\uFF1B\u6CE2\u7EB9\u4E5F\u8DDF\u7740\u7CCA\u6389
    float edge = 0.05 + 0.3 * p.x * p.x + 0.04 * cos(p.x * 14.0 + ph * 0.5) * sharp;
    float r = max(length(vec2(p.x, min(p.y, 0.0))), (p.y - edge) / (0.3 + soft) + 1.0);
    float fill = smoothstep(1.0 + soft, 1.0 - soft, r);
    float rr = (r - 0.9) / (0.09 + soft);
    float rim = exp(-rr * rr);
    vec2 pc = p + vec2(0.0, 0.4);
    float lobe = length(pc) - 0.26 - 0.07 * cos(4.0 * atan(pc.y, pc.x));
    float gon = exp(-lobe * lobe / (0.004 + soft * soft)) * (0.4 + 0.6 * sharp);
    c += jc * (fill * (0.16 + 0.55 * rim + 0.45 * gon) + glowPt(d + vec2(0.0, size * 0.25), size * 0.9, b, 0.0) * 0.18);
    if (d.y <= 0.0) continue;
    // \u4F1E\u4E0B\u4E00\u9053\u62D6\u5C3E\u5149\uFF1A\u6A21\u7CCA\u65F6\u9760\u5B83\u8BA4\u51FA\u662F\u6C34\u6BCD
    float we = sqrt(pow(wx * 0.55, 2.0) + b * b);
    c += jc * exp(-d.x * d.x / (we * we)) * pow(max(1.0 - d.y / (size * 3.5), 0.0), 2.0) * 0.12 * smoothstep(0.0, size * 0.3, d.y);
    if (!fine) continue;
    // \u89E6\u624B\uFF1A\u4F1E\u88D9\u4E0B\u6CBF\u5782\u4E0B\u51E0\u6839\u7EC6\u4E1D\uFF0C\u987A\u7740\u5F80\u4E0B\u4F20\u7684\u6CE2\u98D8
    float w = 0.0012 + size * 0.012;
    we = sqrt(w * w + b * b);
    for (float k = 0.0; k < 6.0; k++) {
      float len = size * (2.6 + 1.6 * h1(float(i) * 13.0 + k));
      float s = d.y / len;
      if (s > 1.05) continue;
      float tx = (k / 5.0 - 0.5) * 1.5 * wx * (1.0 - 0.35 * min(s, 1.0));
      float dx = d.x - tx - sin(d.y / size * 2.6 - ph * 0.9 + k * 1.3) * size * 0.28 * min(s, 1.0);
      c += jc * (w / we) * exp(-dx * dx / (we * we)) * pow(max(1.0 - s, 0.0), 1.5) * smoothstep(hy * 0.1, hy * 0.35, d.y) * 0.8;
    }
    // \u4E2D\u95F4\u7684\u53E3\u8155\uFF1A\u7C97\u4E00\u70B9\u3001\u77ED\u4E00\u70B9\u7684\u4E00\u6761\u98D8\u5E26
    float s = d.y / (size * 1.7);
    if (s < 1.0) {
      float wa = size * 0.14 * (1.0 - s * 0.6), wae = sqrt(wa * wa + b * b);
      float dx = d.x - sin(d.y / size * 3.5 - ph + 1.0) * size * 0.12 * s;
      c += jc * (wa / wae) * exp(-dx * dx / (wae * wae)) * (1.0 - s) * 0.35 * smoothstep(0.0, 0.01, d.y);
    }
  }
  return c;
}
`;var Et=`
float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
// \u683C\u5B50\u7F16\u53F7 \u2192 0..1 \u7684\u4F2A\u968F\u673A\u6570\uFF08Dave Hoskins \u7684 hash11\uFF09\u3002\u539F\u6765\u7528 fract(sin(n) \xD7 43758)\uFF0Csin \u7684\u7CBE\u5EA6\u5404\u5BB6\u9A71\u52A8\u4E0D\u4E00\u6837\u3001
// \u7F16\u53F7\u968F\u884C\u9A76\u8D8A\u6765\u8D8A\u5927\u65F6\u5E73\u53F0\u95F4\u4F1A\u5BF9\u4E0D\u4E0A\uFF1B\u7EAF\u7B97\u672F\u7684\u5206\u5E03\u4E00\u6837\uFF08\u5747\u5300\u3001\u76F8\u90BB\u7F16\u53F7\u4E0D\u76F8\u5173\uFF09\uFF0C\u53EA\u662F\u6BCF\u683C\u5177\u4F53\u7684\u503C\u6362\u4E86
float h1(float n) { n = fract(n * 0.1031); n *= n + 33.33; n *= n + n; return fract(n); }
float vnoise(vec2 p) {
  vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
vec3 pick(float h) { return h < 0.34 ? uLa : (h < 0.67 ? uLb : uLc); }
// \u9AD8\u65AF\u5149\u70B9\uFF1Ar \u5149\u70B9\u534A\u5F84\uFF0Cb \u6A21\u7CCA\u534A\u5F84\uFF0Cstreak \u6A2A\u5411\u62D6\u5F71\u534A\u957F\uFF1B\u6A21\u7CCA\u8D8A\u5927\u8D8A\u6697\u3001\u8D8A\u6563
float glowPt(vec2 d, float r, float b, float streak) {
  d.x = sign(d.x) * max(abs(d.x) - streak, 0.0);
  float s2 = r * r + b * b;
  return (r * r / s2) * exp(-dot(d, d) / s2 * 1.6);
}
// \u8F6F\u8FB9\u77E9\u5F62\uFF1Asize \u534A\u5BBD\u9AD8\uFF0Csoft \u6A21\u7CCA\u534A\u5F84
float softBox(vec2 d, vec2 size, float soft) {
  vec2 q = abs(d) - size;
  float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
  float k = min(size.x, size.y);
  return (1.0 - smoothstep(-soft * 0.5, soft, sd)) * k / (k + soft * 0.6);
}
// \u534A\u5F84 r\u3001\u6A21\u7CCA b \u7684 glowPt \u7EB5\u5411\u7167\u5F97\u5230\u591A\u8FDC\uFF1A\u518D\u8FDC\u6307\u6570\u9879 < 1e-6\uFF0C\u4E58\u4E0A\u6700\u5927\u7684\u500D\u6570\uFF08\u9713\u8679 2.8 \xD7 uNeon 2\uFF09\u4E5F\u8FDC\u4E0D\u5230 8 \u4F4D\u8F93\u51FA\u7684\u4E00\u7EA7\u3002
// \u4E0B\u9762\u5404\u7EC4\u706F\u7EB5\u5411\u7167\u4E0D\u5230\u5F53\u524D\u8FD9\u4E00\u884C\u5C31\u6574\u7EC4\u8DF3\u8FC7\uFF0C\u753B\u9762\u4E0D\u53D8
float reach(float r, float b) { return 3.0 * sqrt(r * r + b * b); }
const float HZ = 0.6, ROAD = 0.7;
const float WIND = 0.6;   // \u8F66\u901F 1 \u65F6\uFF0C\u96E8\u76F8\u5BF9\u8F66\u8EAB\u6BCF\u5F80\u4E0B 1 \u5C31\u5F80\u540E 0.6\uFF08\u548C JS \u91CC\u7684 WIND_DRIFT \u5BF9\u5E94\uFF09
const vec3 SODIUM = vec3(1.0, 0.68, 0.32);
// uScenery \u7684\u4F4D\uFF1A\u7A97\u5916\u753B\u54EA\u51E0\u6837\uFF08\u8BBE\u7F6E\u91CC\u7684\u5F00\u5173\uFF0CnebbiaFrameUniforms.ts\uFF09\u3002\u805A\u5149\u706F\u4E0D\u5360\u4F4D\uFF0C\u5173\u6389\u65F6 uSpotI \u4E3A 0
const int SCN_BUILDINGS = 1, SCN_LIGHTS = 2, SCN_CARS = 4, SCN_ROAD = 8, SCN_RAIN = 16, SCN_RAIN_SLANT = 32;
bool sceneryOn(int bit) { return (uScenery & bit) != 0; }
`;var At=`
// \u805A\u5149\u706F\u5149\u67F1\uFF1A\u4ECE\u697C\u5E95\u7684\u5149\u6E90\u5C04\u5411\u73BB\u7483\u4E0A\u7684\u5149\u6591\uFF0C\u8D8A\u8FD1\u8D8A\u5BBD\u8D8A\u4EAE\uFF1Bb \u662F\u6A21\u7CCA\u534A\u5F84\uFF08\u96FE\u540E\u9762\u770B\u5149\u67F1\u66F4\u6563\uFF09
float beamFrom(vec2 src, vec2 q, float b) {
  vec2 d = uSpotTgt - src;
  float L = length(d);
  if (uSpotI <= 0.0 || L < 1e-3) return 0.0;
  d /= L;
  vec2 rel = q - src;
  float along = dot(rel, d);
  // \u5230\u300C\u5149\u6E90\u2014\u5149\u6591\u300D\u8FD9\u6761\u7EBF\u6BB5\u7684\u8DDD\u79BB\uFF0C\u4E0D\u662F\u5230\u65E0\u9650\u957F\u4E2D\u8F74\u7EBF\u7684\uFF1A\u672B\u7AEF\u662F\u4EE5\u5149\u6591\u4E3A\u5706\u5FC3\u7684\u5706\u5934\u3002
  // \u539F\u6765\u6309\u4E2D\u8F74\u7EBF\u7B97\u3001\u518D\u6CBF\u5149\u67F1\u65B9\u5411\u5E73\u5207\uFF0C\u672B\u7AEF\u662F\u4E00\u9053\u548C\u5149\u67F1\u4E00\u6837\u5BBD\u7684\u5E73\u53E3\uFF1B\u4E24\u76CF\u706F\u4ECE\u4E0D\u540C\u89D2\u5EA6\u6253\u5230\u540C\u4E00\u5904\uFF0C
  // \u4E24\u4E2A\u5E73\u53E3\u7684\u89D2\u9732\u5728\u5149\u6591\u4E24\u4FA7\uFF0C\u770B\u7740\u50CF\u4E2A X
  float perp = length(rel - d * clamp(along, 0.0, L));
  float k = clamp(along / L, 0.0, 1.0);
  float w = mix(0.004, uSpotR * 0.9, k) + b;
  float body = exp(-perp * perp / (w * w)) * smoothstep(-0.01, 0.04, along) * (0.25 + 0.75 * k * k);
  // \u5149\u6E90\u672C\u8EAB\uFF1A\u697C\u5E95\u4E00\u9897\u5F88\u4EAE\u7684\u706F
  float lamp = exp(-dot(rel, rel) / (0.00004 + b * b * 0.25)) * 2.5 / (1.0 + b * 50.0);
  return (body * 0.7 + lamp) * uSpotI;
}
// \u5DE6\u53F3\u4E24\u76CF\uFF0C\u5149\u6E90\u5DE6\u53F3\u5BF9\u79F0\uFF0C\u6253\u5230\u540C\u4E00\u4E2A\u5B57\u4E0A
float spotBeam(vec2 q, float b) { return beamFrom(uSpotSrc, q, b) + beamFrom(uSpotSrc2, q, b); }
`;var Cn=`${Et}${At}${Dn}${Pn}
// \u300C\u821E\u6F6E\u8E0F\u62CD\u300D\u7684\u6C34\u8349\u4E00\u9879\uFF08nebbiaWeedSway.ts\uFF09\uFF1A\u5F39\u7C27\u73B0\u5728\u30010.05\u30010.1\u30010.15 \u79D2\u524D\u7684\u4F4D\u7F6E\uFF1B\u6839\u53D6\u73B0\u5728\u3001\u68A2\u53D6 0.15 \u79D2\u524D\uFF0C
// \u4ECE\u6839\u5F80\u68A2\u4F20
uniform vec4 uSway;
// \u6C34\u8349\u81EA\u5DF1\u7684\u6446\u52A8\u7559\u591A\u5C11\uFF1A1 \u7167\u5E38\uFF0C\u821E\u6F6E\u8E0F\u62CD\u7684\u6C34\u8349\u4E00\u9879\u5F00\u7740\u65F6 0\uFF08\u53EA\u8DDF\u62CD\u8D70\uFF09\uFF0C\u5207\u6362\u65F6\u6162\u6162\u8FC7\u6E21\uFF08nebbiaAquarium.ts\uFF09
uniform float uWeedIdle;
float weedKick(float s) {
  float f = clamp(s, 0.0, 1.0) * 3.0;
  return f < 1.0 ? mix(uSway.x, uSway.y, f) : f < 2.0 ? mix(uSway.y, uSway.z, f - 1.0) : mix(uSway.z, uSway.w, f - 2.0);
}
vec3 scene(vec2 q, float b) {
  float t = uTime, sharp = 1.0 - clamp(b * 25.0, 0.0, 1.0);
  vec3 deep = uSky0 * 0.55;
  vec3 col = mix(mix(uSky1, uLa, 0.3) * 1.25 + 0.02, deep, smoothstep(0.0, 0.95, q.y));
  vec3 lightC = mix(uLa, vec3(1.0), 0.45);
  // \u6C34\u9762\u7167\u4E0B\u6765\u7684\u5149\u675F\uFF0C\u8D8A\u5F80\u4E0B\u8D8A\u6DE1\uFF1B\u6700\u4E0A\u9762\u4E00\u5C42\u6C34\u9762\u7684\u7CBC\u5149
  float rx = q.x + q.y * 0.35;
  float rays = pow(0.5 + 0.5 * sin(rx * 9.0 + t * 0.3 + sin(rx * 3.1 - t * 0.2) * 1.5), 6.0)
             + 0.6 * pow(0.5 + 0.5 * sin(rx * 17.0 - t * 0.45), 8.0);
  col += lightC * mix(0.25, rays, sharp * 0.6 + 0.4) * (1.0 - smoothstep(0.0, 0.85, q.y)) * 0.13;
  col += lightC * 0.8 * exp(-q.y * 14.0) * (0.25 + 0.15 * sin(q.x * 30.0 + t * 1.3 + sin(q.x * 7.0)) * sharp);
  // \u6C99\u5E95\u548C\u7126\u6563\uFF08\u300C\u9A6C\u8DEF\u300D\uFF09
  if (sceneryOn(SCN_ROAD)) {
    float floorY = 0.89 + 0.015 * sin(q.x * 3.0);
    float caus = pow(abs(sin(q.x * 23.0 + sin(q.y * 40.0 + t) * 1.2 + t * 0.6) * sin(q.x * 17.0 - t * 0.5 + q.y * 30.0)), 3.0);
    col = mix(col, deep * 1.3 + 0.03 + lightC * caus * 0.12 * sharp, smoothstep(floorY - 0.01 - b, floorY + 0.01 + b, q.y));
  }
  // \u7901\u77F3\uFF08\u300C\u697C\u7FA4\u300D\uFF09\uFF1A\u9876\u4E0A\u88AB\u6C34\u9762\u7684\u5149\u7167\u4EAE\u3001\u5E26\u4E00\u70B9\u7126\u6563\uFF0C\u5F80\u4E0B\u6E10\u6697\uFF0C\u8868\u9762\u6709\u5751\u6D3C\u7684\u7EB9\u7406\uFF1B\u6BD4\u540E\u9762\u7684\u6C34\u4EAE\u4E00\u70B9\u624D\u770B\u5F97\u51FA\u662F\u77F3\u5934
  if (sceneryOn(SCN_BUILDINGS)) {
    float rockY = 0.8 + 0.07 * vnoise(vec2(q.x * 3.5, 1.0)) + 0.02 * vnoise(vec2(q.x * 14.0, 3.0));
    float under = max(q.y - rockY, 0.0);
    float tex = 0.75 + 0.5 * mix(0.5, vnoise(q * vec2(26.0, 34.0)) * 0.65 + vnoise(q * vec2(70.0, 90.0)) * 0.35, sharp);
    vec3 rock = mix(deep * 1.6 + lightC * 0.07, deep * 0.75, smoothstep(0.0, 0.09, under)) * tex;
    float rc = pow(abs(sin(q.x * 31.0 + t * 0.5 + sin(q.y * 50.0 - t * 0.7)) * sin(q.x * 19.0 - t * 0.4 + q.y * 37.0)), 4.0);
    rock += lightC * (0.16 * exp(-under * 70.0) + rc * 0.07 * sharp * (1.0 - smoothstep(0.0, 0.05, under)));
    col = mix(col, rock, smoothstep(rockY - b - 0.003, rockY + b + 0.003, q.y) * 0.95);
    // \u77F3\u810A\u524D\u9762\u4E00\u5757\u5757\u5706\u77F3\u5934\uFF1A\u6309\u692D\u7403\u7684\u6CD5\u7EBF\u7B97\u53D7\u5149\uFF08\u5149\u4ECE\u6C34\u9762\u5F80\u4E0B\u6253\uFF09\uFF0C\u9876\u4E0A\u4EAE\u3001\u5E95\u4E0B\u6697\uFF0C\u8FB9\u7F18\u88AB\u566A\u58F0\u5543\u5F97\u4E0D\u5706
    float rw = 0.17, rcx = floor(q.x / rw);
    for (float k = -1.0; k <= 1.0; k++) {
      float n = rcx + k, h = h1(n * 4.1 + 2.0);
      if (h > 0.7) continue;
      float rx = 0.06 + 0.05 * h1(n + 9.0), ry = rx * (0.55 + 0.3 * h1(n * 2.7));
      vec2 c = vec2((n + 0.3 + 0.4 * h1(n * 5.3)) * rw, 0.95);
      vec2 d = (q - c) / vec2(rx, ry);
      float edge = 0.12 * (vnoise(vec2(atan(d.y, d.x) * 2.5, n * 3.0)) - 0.5);
      float e = length(d) + edge, soft = (b + 0.003) / ry;
      float cover = smoothstep(1.0 + soft, 1.0 - soft, e);
      if (cover <= 0.0) continue;
      vec3 nrm = normalize(vec3(d * 0.9, sqrt(max(1.0 - dot(d, d), 0.04))));
      float lit = max(dot(nrm, normalize(vec3(-0.35, -1.0, 0.7))), 0.0);
      vec3 stone = (deep * 1.8 + lightC * 0.08) * (0.4 + 1.0 * lit) * tex + lightC * pow(lit, 6.0) * 0.16 * sharp;
      col = mix(col, stone, cover * 0.97);
    }
  }
  // \u6C34\u8349\uFF08\u300C\u697C\u7FA4\u300D\uFF09\u548C\u957F\u5728\u4E0A\u9762\u7684\u4E00\u4E32\u4E32\u8367\u5149\u7C92\u5B50\uFF08\u300C\u5149\u6591\u300D\uFF09\uFF1A\u6C34\u8349\u4ECE\u6839\u5230\u68A2\u4F20\u4E00\u9053\u6CE2\uFF0C\u6446\u5F97\u5FEB\u4E00\u70B9\uFF0C
  // \u300C\u821E\u6F6E\u8E0F\u62CD\u300D\u65F6\u518D\u8DDF\u62CD\u5F80\u4E00\u8FB9\u63A8\uFF08weedKick\uFF09\uFF1B\u7C92\u5B50\u8DDF\u7740\u8349\u8EAB\u4E00\u8D77\u6446\uFF0C\u8D8A\u5F80\u68A2\u8D8A\u4EAE\uFF0C\u5404\u81EA\u4E00\u95EA\u4E00\u95EA
  bool weeds = sceneryOn(SCN_BUILDINGS), glow = sceneryOn(SCN_LIGHTS);
  if (weeds || glow) {
    float cw = 0.06, cx = floor(q.x / cw);
    for (float k = -1.0; k <= 1.0; k++) {
      float n = cx + k, h = h1(n * 1.7 + 5.0);
      if (h > 0.45) continue;
      float top = 0.55 + 0.25 * h1(n + 2.0), base = 0.92, len = base - top;
      float sway = t * (3.6 + 1.8 * h) + h * 20.0, bend = 0.07 * (0.7 + 0.6 * h);
      if (weeds) {
        float s = clamp((base - q.y) / len, 0.0, 1.0);
        float x0 = (n + 0.5) * cw + sin(q.y * 11.0 + sway) * 0.025 * uWeedIdle * s + weedKick(s) * bend * s * s;
        float w = 0.003 * (1.0 - s * 0.6), we = sqrt(w * w + b * b), dx = q.x - x0;
        float a = (w / we) * exp(-dx * dx / (we * we)) * smoothstep(top - b, top + 0.02 + b, q.y) * step(q.y, base) * 0.5;
        col = mix(col, mix(deep, vec3(0.02, 0.08, 0.05), 0.6), a);
      }
      // \u8349\u68A2\u4E0A\u9762\u79BB\u7C92\u5B50\u592A\u8FDC\uFF08\u96FE\u7684\u9884\u6E32\u67D3\u8981\u7B97\u4E0A\u5149\u6655\u7CCA\u5F00\u7684\u8303\u56F4\uFF09\u5C31\u8DF3\u8FC7
      if (!glow || q.y < top - 0.05 - b * 3.0) continue;
      float s = clamp((base - q.y) / len, 0.0, 1.0);
      float N = 11.0, gap = len / (N - 1.0);
      // \u96FE\u540E\u9762\uFF08\u9884\u6E32\u67D3\uFF09\u7C92\u5B50\u7CCA\u6210\u4E00\u6761\uFF1A\u6309\u8FDE\u7EED\u7684\u4E00\u6761\u5149\u5E26\u7B97\uFF0C\u4E0D\u4E00\u9897\u9897\u7B97\uFF0C\u514D\u5F97\u5149\u6655\u6BD4\u95F4\u8DDD\u5BBD\u65F6\u6F0F\u7B97\u51FA\u65AD\u5C42
      if (b > 0.012) {
        float xs = (n + 0.5) * cw + sin(q.y * 11.0 + sway) * 0.025 * uWeedIdle * s + weedKick(s) * bend * s * s, dx = q.x - xs;
        float fj = s * (N - 1.0), ja = floor(fj);
        vec3 pc = mix(mix(pick(h1(n * 3.0 + ja)), pick(h1(n * 3.0 + ja + 1.0)), fj - ja), vec3(1.0), 0.25);
        float line = 0.0;
        for (float r3 = 0.0; r3 < 3.0; r3++) {
          float r = r3 < 0.5 ? 0.0018 + 0.0018 * s : r3 < 1.5 ? (0.0018 + 0.0018 * s) * 4.0 : 0.009 + 0.008 * s;
          float wgt = r3 < 0.5 ? 1.6 : r3 < 1.5 ? 0.25 : 0.4;
          // \u4E00\u9897 glowPt \u6CBF\u7AD6\u76F4\u65B9\u5411\u79EF\u5206\uFF0C\u518D\u9664\u4EE5\u95F4\u8DDD\uFF1A\u4E00\u4E32\u7C92\u5B50\u7684\u5E73\u5747\u4EAE\u5EA6
          float s2 = r * r + b * b;
          line += wgt * (r * r / s2) * exp(-dx * dx / s2 * 1.6) * sqrt(3.1416 * s2 / 1.6) / gap;
        }
        float e = 0.009 + b;
        line *= smoothstep(top - e, top + e, q.y) * smoothstep(base + e, base - e, q.y);
        col += pc * line * 0.7 * (0.35 + 0.65 * s) * uNeon;
        continue;
      }
      // \u6E05\u6670\u7684\u753B\u9762\u4E00\u9897\u9897\u7B97\uFF1A\u6700\u8FD1\u7684\u4E00\u9897\u524D\u540E\u5404\u4E24\u9897\uFF08\u5149\u6655\u6BD4\u95F4\u8DDD\u5BBD\uFF09\uFF0C\u8D85\u51FA\u4E24\u5934\u7684\u4E0D\u7B97\uFF0C\u4E0D\u4F1A\u628A\u68A2\u4E0A\u90A3\u9897\u7B97\u4E24\u904D
      float j0 = floor(s * (N - 1.0) + 0.5);
      for (float dj = -2.0; dj <= 2.0; dj++) {
        float j = j0 + dj, sj = j / (N - 1.0);
        if (j < 0.0 || j > N - 1.0) continue;
        float py = base - sj * len;
        vec2 pp = vec2((n + 0.5) * cw + sin(py * 11.0 + sway) * 0.025 * uWeedIdle * sj + weedKick(sj) * bend * sj * sj, py);
        float tw = 0.45 + 0.55 * pow(0.5 + 0.5 * sin(t * (2.5 + h1(n * 9.0 + j) * 3.0) + j * 1.7 + h * 30.0), 2.0);
        float pr = 0.0018 + 0.0018 * sj;
        vec3 pc = mix(pick(h1(n * 3.0 + j)), vec3(1.0), 0.25);
        // \u6BCF\u9897\u5E26\u4E00\u5708\u5927\u5149\u6655\uFF1A\u96FE\u540E\u9762\u7EC6\u5C0F\u7684\u4EAE\u70B9\u7CCA\u6CA1\u4E86\uFF0C\u5149\u6655\u7CCA\u5F00\u8FD8\u662F\u4E00\u56E2\u56E2\u5F69\u8272\u7684\u5149
        vec2 d = q - pp;
        col += pc * (glowPt(d, pr, b, 0.0) * 1.6 + glowPt(d, pr * 4.0, b, 0.0) * 0.25 + glowPt(d, 0.009 + 0.008 * sj, b, 0.0) * 0.4) * tw * (0.35 + 0.65 * sj) * uNeon;
      }
    }
  }
  col += jellies(q, b);
  col = aqBubbles(col, q, b, mix(lightC, vec3(1.0), 0.4));
  // \u805A\u5149\u706F\uFF1A\u4ECE\u6C34\u9762\u4E0A\u65B9\u6253\u5230\u6B63\u5728\u5199\u7684\u5B57\u4E0A
  col += lightC * spotBeam(q, b) * 0.8;
  return col;
}
`;var Gn=`${Et}// \u8DEF\u706F\u548C\u62DB\u724C\uFF08\u5012\u5F71\u4E5F\u7528\u5B83\uFF09
vec3 lampsSigns(vec2 q, float b, float t) {
  vec3 c = vec3(0.0);
  // \u62DB\u724C\u3001\u5E97\u9762\uFF1A\u8FD1\uFF0C\u79FB\u52A8\u5FEB\uFF0C\u5E26\u6A2A\u5411\u62D6\u5F71
  float xs = q.x + t * 0.42, cw = 0.34, streak = 0.012 * uBus;
  float cs = floor(xs / cw);
  // \u62DB\u724C\u7684\u706F\u5728 0.46\u20130.56 \u9AD8\u3001\u5E97\u9762\u5728 0.645\uFF08\u5012\u5F71\u8C03\u7528\u65F6 q\u3001b \u4E0D\u4E00\u6837\uFF0C\u5404\u81EA\u5224\u65AD\uFF09
  bool sg = abs(q.y - 0.51) < 0.05 + reach(0.016, b + 0.002), sh = abs(q.y - 0.645) < reach(0.026, b + 0.003);
  if (sg || sh) for (float k = -1.0; k <= 1.0; k++) {
    float n = cs + k;
    // \u62DB\u724C\u3001\u5E97\u9762\u7684\u706F\uFF1A\u90FD\u753B\u6210\u5706\u7684\u6563\u666F\u5149\u6591\uFF08\u8FDC\u5904\u7684\u706F\u5931\u7126\u540E\u662F\u5706\u7684\uFF09\uFF0C\u4E0D\u753B\u65B9\u5757
    if (sg) {
      vec2 cen = vec2((n + 0.2 + 0.6 * h1(n)) * cw, 0.46 + 0.1 * h1(n + 17.0));
      if (h1(n + 33.0) < 0.75) c += pick(h1(n + 2.0)) * glowPt(vec2(xs, q.y) - cen, 0.006 + 0.01 * h1(n + 5.0), b + 0.002, streak) * 1.6;
    }
    // \u5E97\u9762\uFF1A\u5E95\u5C42\u4E00\u4E24\u56E2\u6696\u5149
    vec2 shop = vec2((n + 0.5) * cw, 0.645);
    if (sh && h1(n + 41.0) < 0.6) c += mix(SODIUM, pick(h1(n + 8.0)), 0.3) * glowPt(vec2(xs, q.y) - shop, 0.016 + 0.01 * h1(n + 3.0), b + 0.003, streak) * 0.9;
  }
  // \u8DEF\u706F\uFF1A\u4E2D\u666F\uFF0C\u706F\u5934 + \u96E8\u91CC\u7684\u5927\u5149\u6655\uFF080.36 \u9AD8\uFF0C\u5149\u6655\u534A\u5F84 0.07\uFF09
  float xl = q.x + t * 0.22, lw = 0.55;
  float cl = floor(xl / lw);
  if (abs(q.y - 0.36) < reach(0.07, b)) for (float k = -1.0; k <= 1.0; k++) {
    vec2 d = vec2(xl - (cl + k + 0.5) * lw, q.y - 0.36);
    c += SODIUM * (glowPt(d, 0.007, b, 0.004 * uBus) * 2.6 + glowPt(d, 0.07, b, 0.0) * 0.3);
  }
  return c;
}
// \u4E00\u5C42\u697C\u7FA4\uFF08layer 0 = \u8FDC\uFF0C1 = \u8FD1\uFF09\u3002\u8FD4\u56DE rgb = \u989C\u8272\uFF0Ca = \u8986\u76D6\u5EA6\u3002
// \u6BCF\u683C\u4E00\u680B\uFF1A\u5BBD\u5EA6\u3001\u9AD8\u5EA6\u968F\u673A\uFF0C\u4E00\u534A\u5E26\u6536\u5206\u7684\u9876\u5C42\uFF0C\u5C11\u6570\u9876\u4E0A\u6709\u5929\u7EBF\uFF1B\u7ACB\u9762\u4E0B\u4EAE\u4E0A\u6697\uFF08\u88AB\u8857\u706F\u7167\uFF09\u3001\u4E00\u4FA7\u6709\u4E00\u9053\u8FB9\u5149\uFF1B
// \u7A97\u662F\u5C0F\u683C\u5B50\uFF0C\u6BCF\u680B\u4EAE\u706F\u6BD4\u4F8B\u4E0D\u540C\uFF0C\u6709\u7684\u6574\u5C42\u5168\u9ED1\uFF0C\u4EAE\u7740\u7684\u7A97\u5206\u6696\u767D\u3001\u51B7\u767D\u548C\u4E3B\u9898\u8272\uFF0C\u4EAE\u5EA6\u4E0D\u4E00\u3002
vec4 building(vec2 q, float b, float t, float layer) {
  float near = layer;
  float x = q.x + t * mix(0.02, 0.035, near) + layer * 7.7;
  float cw = mix(0.065, 0.12, near), aa = max(b, 0.0012);
  float c0 = floor(x / cw);
  vec4 res = vec4(0.0);
  // \u6BCF\u680B\u697C\uFF08\u8FDE\u6536\u5206\u9876\u5C42\u3001\u5929\u7EBF\uFF09\u90FD\u843D\u5728\u81EA\u5DF1\u90A3\u4E00\u683C\u91CC\uFF0C\u5DE6\u53F3\u90BB\u5C45\u53EA\u5728\u79BB\u683C\u8FB9\u4E0D\u5230\u6A21\u7CCA\u534A\u5F84\uFF08\u518D\u52A0\u5929\u7EBF\u5BBD\uFF09\u65F6\u624D\u78B0\u5F97\u5230\u8FD9\u4E2A\u50CF\u7D20\uFF1A
  // \u79BB\u5F97\u66F4\u8FDC\u65F6\u90BB\u5C45\u7684\u8986\u76D6\u5EA6\u5FC5\u7136\u662F 0\uFF0C\u8DF3\u8FC7\uFF0C\u753B\u9762\u4E0D\u53D8\u3002\u6E05\u6670\u7A97\u5916\u6A21\u7CCA\u534A\u5F84\u5F88\u5C0F\uFF0C\u5927\u90E8\u5206\u50CF\u7D20\u53EA\u7528\u7B97\u672C\u683C\u4E00\u680B
  float fx = x - c0 * cw, nbReach = aa + 0.0013;
  for (float k = -1.0; k <= 1.0; k++) {
    if ((k < 0.0 && fx > nbReach) || (k > 0.0 && cw - fx > nbReach)) continue;
    float n = c0 + k + layer * 100.0;
    float w = cw * (0.72 + 0.26 * h1(n));
    float left = (c0 + k) * cw + (cw - w) * h1(n + 1.0);
    float lx = x - left;
    float hgt = mix(0.07, 0.11, near) + mix(0.16, 0.27, near) * pow(h1(n + 2.0), 1.5);
    float base = HZ + near * 0.015;
    float top = base - hgt;
    // \u6536\u5206\u7684\u9876\u5C42
    bool tier = h1(n + 3.0) < 0.5;
    float tw = w * (0.5 + 0.25 * h1(n + 4.0)), tl = (w - tw) * 0.5, th = hgt * (0.12 + 0.15 * h1(n + 5.0));
    float body = smoothstep(-aa, aa, lx) * smoothstep(-aa, aa, w - lx) * smoothstep(-aa, aa, q.y - top);
    float up = tier ? smoothstep(-aa, aa, lx - tl) * smoothstep(-aa, aa, tl + tw - lx) * smoothstep(-aa, aa, q.y - (top - th)) : 0.0;
    float roof = tier ? top - th : top;
    // \u5929\u7EBF
    float ant = h1(n + 6.0) < 0.3 ? smoothstep(0.0012 + aa, 0.0, abs(lx - w * 0.5)) * smoothstep(-aa, aa, q.y - (roof - 0.04 - 0.03 * h1(n + 7.0))) : 0.0;
    float cov = max(max(body, up), ant);
    if (cov <= 0.0) continue;
    // \u7ACB\u9762\uFF1A\u6697\u8272\uFF0C\u6309\u697C\u7565\u6709\u5DEE\u522B\uFF1B\u8D8A\u5F80\u4E0B\u8D8A\u88AB\u8857\u9762\u7684\u706F\u7167\u4EAE\uFF1B\u5DE6\u4FA7\u4E00\u9053\u7EC6\u8FB9\u5149
    vec3 fac = mix(uSky0, vec3(0.015, 0.018, 0.025), 0.55) * (0.7 + 0.6 * h1(n + 8.0));
    float lowLit = pow(clamp((q.y - roof) / (base - roof), 0.0, 1.0), 2.5);
    fac += mix(SODIUM, uLa, 0.5) * 0.1 * lowLit;
    fac += mix(uSky1, uLb, 0.3) * 0.12 * smoothstep(0.003 + aa, 0.0, lx) * body;
    // \u7A97\u683C
    float fh = mix(0.0075, 0.012, near), wc = mix(0.0055, 0.0085, near);
    vec2 g = vec2((lx - wc * 0.6) / wc, (q.y - roof - fh * 0.8) / fh);
    vec2 id = floor(g), f = fract(g);
    float dens = 0.15 + 0.5 * h1(n + 9.0);
    float floorOn = step(0.18, hash(vec2(id.y, n)));
    float on = step(hash(id + n * 3.7), dens) * floorOn;
    // \u7A97\u533A\u8FB9\u754C\u8DDF\u7740\u6A21\u7CCA\u534A\u5F84\u53D8\u8F6F\uFF0C\u7CCA\u5F00\u65F6\u4E0D\u51FA\u786C\u8FB9
    float inside = smoothstep(-aa, aa, lx - wc * 0.6) * smoothstep(-aa, aa, w - wc * 0.6 - lx) * smoothstep(-aa, aa, q.y - roof - fh * 0.8) * smoothstep(-aa, aa, base - fh - q.y);
    float pane = smoothstep(0.32, 0.22, abs(f.x - 0.5)) * smoothstep(0.3, 0.2, abs(f.y - 0.5));
    // \u88AB\u96FE\u7CCA\u5F00\u65F6\u7A97\u683C\u53D8\u6210\u5E73\u5747\u4EAE\u5EA6\uFF0C\u4E0D\u95EA
    float blurMix = clamp(b / (wc * 0.8), 0.0, 1.0);
    float win = mix(pane * on, 0.3 * dens * 0.82, blurMix) * inside * max(body, up);
    float hc = hash(id + n * 1.3);
    vec3 wcol = hc < 0.5 ? vec3(1.0, 0.78, 0.5) : (hc < 0.8 ? vec3(0.78, 0.88, 1.0) : pick(hash(id + 5.0)));
    // \u7CCA\u5F00\u65F6\u6BCF\u683C\u7684\u989C\u8272\u3001\u4EAE\u5EA6\u4E5F\u6362\u6210\u5E73\u5747\u503C\uFF0C\u5426\u5219\u4F1A\u51FA\u73B0\u9A6C\u8D5B\u514B
    wcol = mix(wcol * (0.55 + 0.6 * hash(id + 2.9)), vec3(0.95, 0.82, 0.66) * 0.85, blurMix);
    vec3 bc = fac + wcol * win * mix(0.7, 1.0, near);
    // \u8FDC\u5C42\u88AB\u5927\u6C14\u51B2\u6DE1\uFF0C\u5F80\u5929\u8272\u9760
    bc = mix(bc, mix(uSky0, uSky1, 0.7), mix(0.45, 0.12, near));
    res = vec4(mix(res.rgb, bc, cov), max(res.a, cov));
  }
  return res;
}
// \u4ECE\u697C\u7FA4\u7F13\u5B58\u53D6\u4E00\u5C42\uFF08\u9884\u4E58\u989C\u8272\uFF09\uFF1A\u6A2A\u5411\u6309\u8FD9\u5C42\u7684\u901F\u5EA6\uFF08building \u91CC\u7684 0.02\u30010.035\uFF09\u5E73\u79FB\uFF0C\u73AF\u5F62\u53D6\u6837
vec4 bldAt(sampler2D s, vec2 q, float t, float speed, vec3 g) {
  // \u6CA1\u6709 mipmap\uFF1B\u5728\u5206\u652F\u91CC\u53D6\u6837\uFF0C\u7528 textureLod \u4E0D\u4F9D\u8D56\u5BFC\u6570
  return textureLod(s, vec2((q.x + t * speed) / g.z, (q.y - g.x) / (g.y - g.x)), 0.0);
}
// \u4E3B\u9898\u8272\u9713\u8679\uFF08\u7167\u96E8\u6E56\u7684\u8FDC\u5904\u706F\u5149\uFF09\uFF1A\u697C\u7FA4\u4E4B\u95F4\u5BC6\u5BC6\u7684\u5F69\u8272\u5C0F\u6563\u666F\uFF0C\u5076\u5C14\u4E00\u5927\u56E2\u9713\u8679\u5E26\u4E00\u5708\u5F88\u5927\u7684\u5149\u6655\u3002
// \u989C\u8272\u53D6\u8FD9\u9996\u6B4C\u7684\u4E09\u4E2A\u706F\u5149\u8272\uFF1B\u79FB\u52A8\u901F\u5EA6\u4ECB\u4E8E\u697C\u7FA4\u548C\u8DEF\u706F\u4E4B\u95F4\u3002\u5012\u5F71\u4E5F\u7528\u5B83\u3002
vec3 neonLights(vec2 q, float b, float t) {
  vec3 c = vec3(0.0);
  float bb = b + 0.003;   // \u6563\u666F\u672C\u8EAB\u5C31\u662F\u865A\u7684
  // \u5C0F\u6563\u666F\uFF1A\u4E24\u6392\u683C\u5B50\uFF0C\u6BCF\u683C\u6700\u591A\u4E00\u9897\uFF0C\u843D\u5728\u697C\u7FA4\u90A3\u4E00\u6BB5\u9AD8\u5EA6\u91CC\uFF080.38\u20130.58\uFF09
  if (abs(q.y - (HZ - 0.12)) < 0.1 + reach(0.012, bb)) for (float row = 0.0; row < 2.0; row++) {
    float x = q.x + t * (0.06 + row * 0.03) + row * 7.3, cw = 0.045 - row * 0.012;
    float c0 = floor(x / cw);
    for (float k = -1.0; k <= 1.0; k++) {
      float n = c0 + k + row * 300.0;
      if (h1(n + 11.0) > 0.85) continue;
      vec2 cen = vec2((c0 + k + 0.15 + 0.7 * h1(n)) * cw, HZ - 0.02 - 0.2 * pow(h1(n + 3.0), 1.6));
      float r = 0.004 + 0.008 * pow(h1(n + 6.0), 3.0);
      c += pick(h1(n + 9.0)) * glowPt(vec2(x, q.y) - cen, r, bb, 0.0) * (1.0 + 1.8 * h1(n + 13.0));
    }
  }
  // \u5927\u9713\u8679\uFF1A\u7A00\u758F\uFF0C\u4E00\u4E2A\u4EAE\u6838 + \u4E00\u5708\u5F88\u5927\u7684\u540C\u8272\u5149\u6655\uFF08\u96E8\u6E56\u5DE6\u8FB9\u90A3\u76CF\u6D0B\u7EA2\u706F\uFF09
  float xb = q.x + t * 0.1, bw = 0.75, cb = floor(xb / bw);
  if (abs(q.y - (HZ - 0.14)) < 0.06 + reach(0.13, b)) for (float k = -1.0; k <= 1.0; k++) {
    float n = cb + k + 500.0;
    vec2 d = vec2(xb - (cb + k + 0.2 + 0.6 * h1(n)) * bw, q.y - (HZ - 0.08 - 0.12 * h1(n + 2.0)));
    vec3 lc = pick(h1(n + 4.0));
    c += lc * (glowPt(d, 0.014, bb, 0.0) * 2.0 + glowPt(d, 0.13, b, 0.0) * 0.4);
  }
  return c;
}
${At}
// \u516C\u4EA4\u8F66\u4FA7\u7A97\u5916\u7684\u96E8\u591C\u8857\u666F\uFF1A\u8FDC\u5904\u697C\u7FA4\u6162\u3001\u8DEF\u706F\u4E2D\u901F\u3001\u62DB\u724C\u548C\u8F66\u706F\u5FEB
vec3 scene(vec2 q, float b) {
  float t = uTime * uBus;
  vec3 col = mix(uSky0, uSky1, smoothstep(0.0, HZ, q.y));
  col += mix(uLa, SODIUM, 0.4) * 0.14 * exp(-abs(q.y - HZ + 0.12) * 6.0);
  // \u697C\u7FA4\uFF1A\u8FDC\u8FD1\u4E24\u5C42\uFF0C\u8FDC\u5C42\u77EE\u3001\u6DE1\u3001\u7A97\u5C0F\uFF1B\u8FD1\u5C42\u9AD8\u3001\u7EC6\u8282\u591A
  float aa = max(b, 0.0015), c0;
  // \u697C\u7FA4\u8D70\u7F13\u5B58\uFF08nebbiaBuildingCache.ts\uFF09\u3002\u6C34\u73E0\u900F\u955C\uFF08b = 0.006\uFF09\u6298\u5C04\u5F97\u8FDC\uFF0C\u4F1A\u53D6\u5230\u7F13\u5B58\u8303\u56F4\u5916\u9762\uFF0C\u7167\u65E7\u73B0\u7B97\uFF08\u53EA\u5360\u51E0\u9897\u6C34\u73E0\u7684\u9762\u79EF\uFF09\uFF1B
  // \u7F13\u5B58\u8FD8\u6CA1\u753B\u597D\u7684\u90A3\u6863\u4E5F\u73B0\u7B97
  bool bldOn = sceneryOn(SCN_BUILDINGS);
  bool cached = b < 0.002 ? uBldOn.x : (b > 0.05 ? uBldOn.z : (b > 0.02 ? uBldOn.y : false));
  if (bldOn && cached) {
    vec4 bf, bn;
    if (b > 0.05) { bf = bldAt(uBld4, q, t, 0.02, uBldGeo[2]); bn = bldAt(uBld5, q, t, 0.035, uBldGeo[2]); }
    else if (b > 0.02) { bf = bldAt(uBld2, q, t, 0.02, uBldGeo[1]); bn = bldAt(uBld3, q, t, 0.035, uBldGeo[1]); }
    else { bf = bldAt(uBld0, q, t, 0.02, uBldGeo[0]); bn = bldAt(uBld1, q, t, 0.035, uBldGeo[0]); }
    col = col * (1.0 - bf.a) + bf.rgb;
    col = col * (1.0 - bn.a) + bn.rgb;
  } else if (bldOn) {
    vec4 bf = building(q, b, t, 0.0);
    col = mix(col, bf.rgb, bf.a);
    vec4 bn = building(q, b, t, 1.0);
    col = mix(col, bn.rgb, bn.a);
  }
  // \u4E3B\u9898\u8272\u9713\u8679\u538B\u5728\u697C\u7FA4\u524D\u9762\uFF1B\u5929\u8FB9\u518D\u94FA\u4E00\u5C42\u5F88\u5BBD\u7684\u4E3B\u9898\u8272\u8F89\u5149
  bool lightsOn = sceneryOn(SCN_LIGHTS);
  if (lightsOn) {
    col += (neonLights(q, b, t) + uLa * 0.12 * exp(-abs(q.y - HZ + 0.1) * 5.0)) * uNeon;
    col += lampsSigns(q, b, t);
  }
  // \u6E7F\u8DEF\u9762\u548C\u5012\u5F71\uFF08\u8DEF\u9762\u5173\u6389\u65F6\u8F66\u706F\u4E5F\u6CA1\u6709\u5012\u5F71\uFF09
  float road = sceneryOn(SCN_ROAD) ? smoothstep(ROAD - aa, ROAD + aa, q.y) : 0.0;
  col = mix(col, uSky0 * 0.5, road * 0.85);
  // \u5012\u5F71\u90FD\u4E58 road\uFF0C\u8DEF\u9762\u4EE5\u4E0A\u6070\u597D\u662F 0\uFF1A\u76F4\u63A5\u8DF3\u8FC7\uFF0C\u753B\u9762\u4E0D\u53D8\uFF0C\u7701\u6389\u5C4F\u4E0A\u5927\u534A\u50CF\u7D20\u7684\u4E24\u6574\u5957\u62DB\u724C\u3001\u9713\u8679\u8BA1\u7B97
  if (road > 0.0 && lightsOn) {
    vec2 qr = vec2(q.x, ROAD - (q.y - ROAD) * 0.55);
    col += lampsSigns(qr, b + 0.012, t) * road * 0.35;
    // \u9713\u8679\u5012\u5F71\uFF1A\u7EB5\u5411\u538B\u5F97\u66F4\u6241\uFF0C\u6240\u4EE5\u5728\u6E7F\u8DEF\u9762\u4E0A\u62C9\u6210\u7AD6\u76F4\u7684\u5149\u67F1\uFF08\u96E8\u6E56\u7684\u5012\u5F71\u505A\u6CD5\uFF09
    vec2 qn = vec2(q.x, HZ - (q.y - ROAD) * 0.22);
    col += neonLights(qn, b + 0.008, t) * road * 0.45 * uNeon;
  }
  // \u8F66\u706F\uFF1A\u540C\u5411\u8F66\u9053\u5C3E\u706F\uFF08\u7EA2\uFF09\uFF0C\u5BF9\u5411\u8F66\u9053\u524D\u706F\uFF08\u767D\uFF09\uFF0C\u5FEB\u901F\u63A0\u8FC7\u62D6\u51FA\u957F\u5F71
  if (sceneryOn(SCN_CARS)) for (float lane = 0.0; lane < 2.0; lane++) {
    float sp = lane < 0.5 ? 0.55 : -1.1, ly = lane < 0.5 ? 0.76 : 0.83, ww = 0.9;
    float xc = q.x + t * sp, cc = floor(xc / ww);
    vec3 lc = lane < 0.5 ? vec3(1.0, 0.18, 0.12) : vec3(1.0, 0.95, 0.85);
    // \u8F66\u706F\u548C\u5B83\u5728\u8DEF\u9762\u4E0A\u7684\u5012\u5F71\u7EB5\u5411\u90FD\u7167\u4E0D\u5230\u8FD9\u4E00\u884C\uFF08\u5012\u5F71\u4E58 road\uFF0C\u8DEF\u9762\u4EE5\u4E0A\u672C\u6765\u5C31\u662F 0\uFF09\u5C31\u8DF3\u8FC7\u8FD9\u6761\u8F66\u9053
    bool hd = abs(q.y - ly) < reach(0.004, b), rf = road > 0.0 && abs((q.y - ly) * 3.0 - 0.03) < reach(0.02, b * 2.0);
    if (!hd && !rf) continue;
    for (float k = -1.0; k <= 1.0; k++) {
      float n = cc + k + lane * 50.0;
      if (h1(n) > 0.6) continue;
      float cx = (cc + k + 0.2 + 0.6 * h1(n + 4.0)) * ww;
      float st = abs(sp) * 0.03 * uBus;
      if (hd) {
        c0 = glowPt(vec2(xc - cx + 0.035, q.y - ly), 0.004, b, st) + glowPt(vec2(xc - cx - 0.035, q.y - ly), 0.004, b, st);
        col += lc * c0 * 2.2;
      }
      if (rf) col += lc * glowPt(vec2(xc - cx, (q.y - ly) * 3.0 - 0.03), 0.02, b * 2.0, st) * road * 0.3;
    }
  }
  // \u7A97\u5916\u7684\u96E8\uFF1A\u76F8\u5BF9\u8F66\u8EAB\u7684\u901F\u5EA6 = \u5F80\u4E0B\u843D + \u5F80\u540E\uFF08\u8F66\u5F80\u53F3\u5F00\uFF0C\u7A97\u5916\u5F80\u5DE6\u8D70\uFF09\uFF0C\u8F66\u8D8A\u5FEB\u8D8A\u659C\uFF1B\u505C\u7AD9\u65F6\u7AD6\u76F4\u843D\u3002
  // \u96E8\u4E1D\u53EA\u5728\u805A\u5149\u706F\u5149\u67F1\u91CC\u753B\uFF08\u88AB\u7167\u4EAE\u7684\u90A3\u4E9B\uFF09\u3002\u8BBE\u7F6E\u91CC\u53EF\u4EE5\u8BA9\u5B83\u603B\u662F\u7AD6\u76F4\u843D\uFF0C\u6216\u8005\u4E0D\u4E0B\uFF08\u53EA\u5269\u5149\u67F1\uFF09
  float beam = spotBeam(q, b);
  if (!sceneryOn(SCN_RAIN)) return col + uLa * beam;
  vec2 rd = normalize(vec2(sceneryOn(SCN_RAIN_SLANT) ? -WIND * uBus : 0.0, 1.0));
  vec2 r = vec2(dot(q, rd), dot(q, vec2(-rd.y, rd.x)));
  float colId = floor(r.y / 0.007), hr = h1(colId);
  float seg = fract(r.x * (1.5 + hr) - uTime * (2.5 + hr * 2.0) * (1.0 + 0.4 * uBus) + hr * 10.0);
  float line = smoothstep(0.0012 + b, 0.0, abs(fract(r.y / 0.007) - 0.5) * 0.007) * smoothstep(0.0, 0.1, seg) * smoothstep(0.3, 0.1, seg);
  // \u805A\u5149\u706F\uFF1A\u5149\u67F1\u53E0\u5728\u6700\u524D\u9762\uFF1B\u5149\u67F1\u91CC\u7684\u96E8\u4E1D\u88AB\u7167\u4EAE
  col += uLa * beam + mix(uLa, vec3(1.0), 0.5) * line * beam * 1.6 * step(0.3, hr) * (1.0 - clamp(b * 40.0, 0.0, 1.0));
  return col;
}
`;var On=`#version 300 es
in vec2 aPos; out vec2 vUv;
void main() { vUv = aPos * 0.5 + 0.5; vUv.y = 1.0 - vUv.y; gl_Position = vec4(aPos, 0.0, 1.0); }`,Wn=o=>`precision highp float;
uniform vec2 uRes, uSceneShift; uniform float uTime, uBus, uNeon;
uniform vec3 uSky0, uSky1, uLa, uLb, uLc;
// \u805A\u5149\u706F\uFF1A\u5149\u6E90\uFF08\u8FDC\u5904\u697C\u5E95\uFF09\u3001\u7167\u5230\u73BB\u7483\u4E0A\u7684\u70B9\u3001\u5149\u6591\u534A\u5F84\uFF0C\u90FD\u5728 scene \u5750\u6807\uFF1BuSpotI \u5F3A\u5EA6
uniform vec2 uSpotSrc, uSpotSrc2, uSpotTgt; uniform float uSpotR, uSpotI;
// \u7A97\u5916\u753B\u54EA\u51E0\u6837\uFF08\u4F4D\u89C1 nebbiaSceneGlsl.ts \u7684 SCN_*\uFF09
uniform int uScenery;
// \u697C\u7FA4\u7F13\u5B58\uFF08nebbiaBuildingCache.ts\uFF09\uFF1A\u4E09\u6863\u6A21\u7CCA \xD7 \u8FDC\u8FD1\u4E24\u5C42\uFF0C\u9884\u4E58\u989C\u8272\uFF1BuBldOn \u6BCF\u6863\u753B\u597D\u4E86\u6CA1\u6709\uFF0CuBldGeo \u6BCF\u6863 (y0, y1, \u6A2A\u5411\u5468\u671F)
uniform bvec3 uBldOn; uniform sampler2D uBld0, uBld1, uBld2, uBld3, uBld4, uBld5; uniform vec3 uBldGeo[3];
in vec2 vUv; layout(location = 0) out vec4 outColor;
${o}`,Oe=Wn(Gn),St=Wn(Cn),qn=o=>`#version 300 es
${o}
uniform float uFogB;
void main() {
  vec2 s0 = vUv + uSceneShift, q0 = vec2(s0.x * uRes.x / uRes.y, s0.y);
  outColor = vec4(scene(q0, uFogB), 1.0);
}`,Un=qn(Oe),Hn=`#version 300 es
${Oe}
uniform float uFillB, uFillLayer, uFillX0; uniform vec3 uFillGeo; uniform vec2 uFillSize;
void main() {
  float span = uFillGeo.z;
  float xw = uFillX0 + fract((gl_FragCoord.x / uFillSize.x * span - uFillX0) / span) * span;
  vec4 r = building(vec2(xw, mix(uFillGeo.x, uFillGeo.y, gl_FragCoord.y / uFillSize.y)), uFillB, 0.0, uFillLayer);
  outColor = vec4(r.rgb * r.a, r.a);
}`,zn=`
vec3 staticNoise(vec2 px) { return vec3(vnoise(px / 6.0) * 0.6 + vnoise(px / 2.2) * 0.4, vnoise(px / 55.0 + 3.1), vnoise(px / 1.6)); }`,Vn=`#version 300 es
${Oe}
${zn}
void main() { outColor = vec4(staticNoise(gl_FragCoord.xy), 1.0); }`,Ue=(o,e=Oe,t=!1)=>`#version 300 es
#define NEBBIA_FOG_TEX ${o?1:0}
${t?`#define NEBBIA_DEW 1
`:""}${e===St?`#define NEBBIA_SOFT_HIGHLIGHT 1
`:""}${e}
uniform sampler2D uMask, uDrop, uField;
#ifdef NEBBIA_DEW
// \u51DD\u9732\u73BB\u7483\u4E0A\u51B7\u51DD\u6C34\u73E0\u7684\u9AD8\u5149\uFF08tuning.dewGlint\uFF09\uFF1B\u7EC6\u6C34\u73E0\u6298\u5C04\u7684\u7A97\u5916\uFF08nebbiaFogGl.ts \u9884\u6E32\u67D3\u7684\u6E05\u6670\u7684\u90A3\u5C42\uFF09
uniform float uDewGlint;
uniform sampler2D uFogSharp;
#endif
// \u54C8\u6C14\u7684\u767D\u6C14\u906E\u7F69\uFF08R \u5F62\u72B6\u3001G \u6D53\u5EA6\uFF0CnebbiaBreath.ts\uFF09\uFF1B\u5C4F\u4E0A\u6CA1\u6709\u54C8\u6C14\u65F6 uBreathOn \u4E3A\u5047\uFF0C\u4E0D\u91C7\u6837
uniform sampler2D uBreath; uniform bool uBreathOn; uniform float uBreathThick;
uniform vec2 uGlassShift; uniform float uMist, uEdge, uRefract, uGlint;
// \u9884\u6E32\u67D3\u597D\u7684\u4E24\u5C42\u96FE\uFF08NEBBIA_FOG_TEX \u4E3A 0 \u65F6\u4E3B\u753B\u9762\u81EA\u5DF1\u7B97\uFF1A\u4E0D\u652F\u6301\u6D6E\u70B9\u6E32\u67D3\u76EE\u6807\u65F6\uFF09
uniform sampler2D uFogBlur, uFogGlow;
// \u9759\u6001\u566A\u58F0\u7EB9\u7406\uFF08nebbiaNoiseTex.ts\uFF09\uFF1A\u6309 px \u53D6\u6837\uFF1BuNoiseOn \u4E3A\u5047\uFF08\u8FD8\u6CA1\u7B97\u597D\u3001\u753B\u5E03\u8D85\u8FC7\u7EB9\u7406\u5C3A\u5BF8\u4E0A\u9650\uFF09\u65F6\u73B0\u7B97
uniform bool uNoiseOn; uniform sampler2D uNoise;
// \u900F\u660E\uFF08\u7A97\u5916\u662F\u5171\u7528\u80CC\u666F\u6216\u684C\u9762\uFF09\uFF1A\u4E0D\u753B\u7A97\u5916\uFF0C\u8F93\u51FA\u9884\u4E58\u7684\u900F\u660E\u5EA6\u3002\u96FE\u662F\u4E00\u5C42\u534A\u900F\u660E\u7684\u96FE\uFF0C\u64E6\u5F00\u7684\u5730\u65B9\u5168\u900F\u660E\uFF0C
// \u5149\uFF08\u805A\u5149\u706F\u3001\u64E6\u75D5\u8FB9\u3001\u6C34\u73E0\u9AD8\u5149\uFF09\u53EA\u52A0\u989C\u8272\u4E0D\u52A0\u4E0D\u900F\u660E\u5EA6\uFF0C\u53E0\u5728\u540E\u9762\u7684\u4E1C\u897F\u4E0A\u3002\u96FE\u7684\u989C\u8272\u8DDF\u4E3B\u9898\u65E5\u591C\uFF1A\u65E5\u95F4\u4E73\u767D\u3001\u591C\u95F4\u6DF1\u8272
uniform sampler2D uWallpaper; uniform bool uSeeThrough; uniform vec3 uMistTint;
${zn}
void main() {
  // \u98A0\u7C38\uFF1A\u73BB\u7483\u4E0A\u7684\u4E1C\u897F\uFF08\u96FE\u3001\u64E6\u75D5\u3001\u6C34\u73E0\uFF09\u8DDF\u8F66\u8EAB\u4E00\u8D77\u6643\uFF0C\u7A97\u5916\u6643\u5F97\u66F4\u591A
  vec2 gv = vUv + uGlassShift, sv = vUv + uSceneShift;
  vec2 px = gv * uRes;
  float aspect = uRes.x / uRes.y;
  vec2 q = vec2(sv.x * aspect, sv.y);
  // \u64E6\u75D5\u8FB9\u7F18\uFF1A\u7EC6\u566A\u58F0\u8BA9\u8FB9\u7F18\u6BDB\u7CD9\uFF0C\u5927\u5757\u566A\u58F0\u8BA9\u91CD\u65B0\u8D77\u96FE\u65F6\u4E00\u7247\u7247\u56DE\u6765
  vec3 nz = uNoiseOn ? texture(uNoise, px / uRes).rgb : staticNoise(px);
  float fine = nz.r, blotch = nz.g;
  // \u54C8\u6C14\uFF1A\u5C31\u662F\u8FD9\u4E00\u7247\u7684\u96FE\u4E00\u4E0B\u53D8\u5BC6\uFF08\u66F4\u7EC6\u5BC6\u7684\u4E00\u5C42\u6C34\u6C7D\u51DD\u4E0A\u6765\uFF09\uFF0C\u4E0D\u662F\u53E6\u76D6\u4E00\u5C42\u3002\u8FB9\u7531\u5927\u5757\u566A\u58F0\u63C9\u6563\u6210\u4E00\u5377\u5377\u7684\u3001
  // \u7EC6\u566A\u58F0\u8BA9\u5B83\u53D1\u6BDB\uFF0C\u6563\u7684\u65F6\u5019\u4ECE\u8584\u7684\u5730\u65B9\u5148\u900F\u3002\u5728\u767D\u6C14\u4E0A\u5199\u7684\u5B57\u7167\u6837\u64E6\u5F00\uFF0C\u6C34\u73E0\u5468\u56F4\u7167\u6837\u62B9\u5F00\u4E00\u5708
  float breath = 0.0, melt = 0.0;
  if (uBreathOn) {
    vec3 bm = texture(uBreath, gv).rgb;
    if (bm.r > 0.003) {
      // \u503C\u566A\u58F0\u76F4\u63A5\u53D6\u9608\u503C\u4F1A\u9732\u51FA\u6A2A\u5E73\u7AD6\u76F4\u7684\u683C\u5B50\uFF1A\u6BCF\u5C42\u8F6C\u4E00\u4E2A\u89D2\u5EA6\u3001\u53E0\u4E09\u5C42\uFF0C\u8FB9\u7F18\u624D\u662F\u4E00\u5377\u5377\u7684
      vec2 r1 = mat2(0.80, -0.60, 0.60, 0.80) * px, r2 = mat2(0.36, 0.93, -0.93, 0.36) * px;
      float bn = vnoise(r1 / 34.0 + 7.0) * 0.55 + vnoise(r2 / 15.0 + 2.3) * 0.3 + vnoise(px / 6.5) * 0.15;
      float bth = 0.3 + (bn - 0.5) * 0.5;
      float cover = smoothstep(bth - 0.2, bth + 0.25, bm.r);
      breath = cover * bm.g * uBreathThick;
      // \u767D\u6C14\u6F2B\u5230\u54EA\uFF0C\u90A3\u513F\u88AB\u76D6\u4F4F\u7684\u65E7\u5B57\u8DDF\u7740\u7CCA\u5F00\uFF08B \u901A\u9053\uFF0C\u53EA\u5728\u9F13\u5F00\u7684\u90A3\u4E00\u9635\uFF09
      melt = cover * bm.b;
    }
  }
  float m = texture(uMask, gv).r;
  // \u88AB\u767D\u6C14\u76D6\u4F4F\u7684\u65E7\u5B57\uFF1A\u906E\u7F69\u5728\u5468\u56F4\u4E00\u5708\u53D6\u51E0\u4E0B\uFF08\u6BCF\u4E2A\u50CF\u7D20\u8F6C\u4E00\u4E2A\u89D2\u5EA6\uFF0C\u7CCA\u51FA\u6765\u662F\u9897\u7C92\u7684\u96FE\u6C14\uFF0C\u4E0D\u662F\u51E0\u9053\u91CD\u5F71\uFF09\uFF0C\u5B57\u5148\u7CCA\u5F00\u3001\u8FB9\u53D8\u8F6F\uFF0C
  // \u518D\u968F yieldFactor \u6DE1\u8FDB\u96FE\u91CC
  if (melt > 0.01) {
    float rad = melt * 0.014 * uRes.y, a0 = hash(px) * 6.2832, acc = 0.0;
    for (int i = 0; i < 8; i++) {
      float a = a0 + float(i) * 0.7854, rr = rad * ((i & 1) == 0 ? 1.0 : 0.5);
      acc += texture(uMask, gv + vec2(cos(a), sin(a)) * rr / uRes).r;
    }
    m = mix(m, acc / 8.0, melt);
  }
  float th = 0.3 + (fine - 0.5) * 0.22 * uEdge + (blotch - 0.5) * 0.12;
  float clear = smoothstep(th - 0.05 - 0.12 * melt, th + 0.06 + 0.06 * melt, m);
  // \u767D\u6C14\u94FA\u5F00\u540E\uFF0C\u76D6\u6389\u7684\u65E7\u5B57\u5728\u906E\u7F69\u4E0A\u7559\u7740 BREATH_GHOST\uFF08nebbiaScene.ts\uFF09\u90A3\u4E48\u4E00\u70B9\uFF1A\u6BD4\u9608\u503C\u4F4E\u3001\u4E0D\u900F\u51FA\u7A97\u5916\uFF0C
  // \u53EA\u8BA9\u90A3\u513F\u7684\u767D\u6C14\u8584\u4E00\u70B9\uFF0C\u7CCA\u7CCA\u7684\u7559\u4E2A\u6D45\u6D45\u7684\u5370\u5B50\u3002\u5728\u5468\u56F4\u56DB\u4E0B\u53D6\u6837\u7CCA\u5F00\uFF1B\u65B0\u5199\u7684\u5B57\u5728\u906E\u7F69\u4E0A\u662F\u6EE1\u7684\uFF0C\u53D6\u5230\u7684\u6709\u4E00\u4E0B\u8D85\u8FC7 0.15
  // \u5C31\u662F\u6328\u7740\u65B0\u5B57\uFF0C\u4E0D\u8584\uFF08\u4E0D\u7136\u65B0\u5B57\u5916\u9762\u4E5F\u8584\u4E00\u5708\uFF09
  if (breath > 0.001) {
    float gr = 0.006 * uRes.y, ga = hash(px + 17.0) * 6.2832, gm = 0.0, gx = m;
    for (int i = 0; i < 4; i++) {
      float a = ga + float(i) * 1.5708, g = texture(uMask, gv + vec2(cos(a), sin(a)) * gr / uRes).r;
      gm += g; gx = max(gx, g);
    }
    breath *= 1.0 - 0.55 * smoothstep(0.0, 0.07, gm * 0.25) * (1.0 - smoothstep(0.1, 0.15, gx));
  }
  // \u96FE\uFF1A\u6A21\u7CCA\u7684\u7A97\u5916\u63D0\u4EAE\u53D1\u7070\uFF0C\u5149\u6655\u6563\u5F97\u66F4\u5F00\uFF0C\u52A0\u6D53\u6DE1\u8D77\u4F0F\u548C\u7EC6\u9897\u7C92\uFF0C\u4E0B\u534A\u90E8\u5206\u7565\u6D53
  float dens = uMist * (0.82 + 0.3 * vnoise(px / 380.0 + uTime * 0.015)) * (0.9 + 0.2 * vUv.y) + breath * 0.9;
  vec3 paper = uSeeThrough ? texture(uWallpaper,vUv).rgb : vec3(0.0);
  vec3 haze = uSeeThrough ? mix(paper,vec3(1.0),0.2) : vec3(0.70, 0.74, 0.78);
  vec3 blur = vec3(0.0), glow = vec3(0.0);
  if (!uSeeThrough) {
#if NEBBIA_FOG_TEX
  // \u9884\u6E32\u67D3\u7EB9\u7406\u7684\u7B2C 0 \u884C\u662F vUv.y = 1\uFF08\u5C4F\u5E55\u5E95\u90E8\uFF09\uFF0C\u6240\u4EE5\u7EB5\u5411\u7FFB\u4E00\u4E0B
  vec2 fu = vec2(vUv.x, 1.0 - vUv.y);
  blur = texture(uFogBlur, fu).rgb;
  glow = texture(uFogGlow, fu).rgb;
#else
  blur = scene(q, 0.03);
  glow = scene(q, 0.09);
#endif
  }
  // \u5BC6\u5230\u666E\u901A\u96FE\u7684\u8303\u56F4\u4EE5\u4E0A\uFF08\u54C8\u6C14\uFF09\u65F6\uFF0C\u7A97\u5916\u518D\u6697\u4E00\u70B9\u3001\u4E73\u767D\u518D\u591A\u4E00\u70B9
  vec3 fog = blur * max(0.08, 1.0 - 0.45 * dens - 0.25 * breath) + glow * 0.9 * dens + haze * (0.2 * dens + 0.25 * breath);
  // \u900F\u660E\u65F6\u96FE\u7684\u6D53\u6DE1\u5C31\u662F\u4E0D\u900F\u660E\u5EA6\uFF0C\u989C\u8272\u662F uMistTint
  float alpha = 1.0;
  if (uSeeThrough) {
    // \u6DF1\u8272\u7684\u96FE\u53E0\u5728\u6697\u684C\u9762\u4E0A\u4E0D\u663E\uFF0C\u6BD4\u767D\u96FE\u6D53\u4E00\u70B9
    alpha = clamp((0.62 * dens + 0.3 * breath) * mix(1.3, 1.0, dot(uMistTint, vec3(0.333))), 0.0, 0.92);
    fog = mix(paper,vec3(0.07),0.55) * alpha;
  }
  // \u900F\u660E\u65F6\u9897\u7C92\u4E58\u4E0D\u900F\u660E\u5EA6\uFF1A\u96FE\u5F88\u6DE1\u7684\u5730\u65B9\u4E0D\u5728\u540E\u9762\u7684\u4E1C\u897F\u4E0A\u6492\u4EAE\u70B9
  fog += (hash(px) - 0.5) * 0.025 * alpha;
  // \u805A\u5149\u706F\u7167\u5230\u73BB\u7483\u4E0A\uFF1A\u96FE\u91CC\u7684\u5C0F\u6C34\u6EF4\u628A\u5149\u6563\u5F00\uFF0C\u5B57\u5468\u56F4\u4E00\u5708\u96FE\u88AB\u7167\u6210\u5F3A\u8C03\u8272
  vec2 sd = q - uSpotTgt;
  fog += (uSeeThrough ? haze : uLa) * uSpotI * exp(-dot(sd, sd) / (uSpotR * uSpotR * 2.2)) * (0.18 + 0.4 * dens);
  fog += smoothstep(0.78, 0.98, nz.b) * 0.05 * dens;
  // \u6C34\u73E0\u7EB9\u7406\uFF1A\u64E6\u75D5\u8FB9 / \u4E0B\u6DCC\u7684\u6C34\u73E0 rd\uFF0C\u96FE\u4E0A\u7684\u51DD\u7ED3\u6C34\u73E0 fd\uFF08\u64E6\u5F00\u7684\u5730\u65B9\u6CA1\u6709\uFF09
  vec4 rd = texture(uDrop, gv);
  vec4 fd = texture(uField, gv) * (1.0 - clear);
  // \u548C raindrop-fx \u4E00\u6837\uFF0C\u6C34\u73E0\u5468\u56F4\u4E00\u5708\u7684\u96FE\u4E5F\u88AB\u62B9\u6389
  clear = max(clear, smoothstep(0.93, 1.0, rd.a));
  vec3 col = fog;
  if (clear > 0.001) {
    // \u64E6\u5F00\u7684\u5730\u65B9\uFF1A\u6E05\u6670\u7684\u7A97\u5916\uFF0C\u7559\u4E00\u5C42\u5F88\u8584\u7684\u6C34\u819C
    vec3 wiped = (uSeeThrough ? vec3(0.0) : scene(q, 0.0012) * 1.05) + haze * 0.02 * dens;
    float rim = clear * (1.0 - clear) * 4.0;
    col = mix(fog, wiped, clear) + rim * haze * 0.16 * dens;
    if (uSeeThrough) alpha = mix(alpha, 0.02 * dens, clear) + rim * 0.16 * dens;
  }
  // \u5408\u6210\u65B9\u5F0F\u7167\u642C raindrop-fx \u7684 compose.glsl\uFF1A\u9608\u503C\u6210\u5F62\u3001\u6309\u6CD5\u7EBF\u6298\u5C04\u6E05\u6670\u7684\u7A97\u5916\u3001\u6309\u5149\u7167\u660E\u6697
  vec4 comp = vec4(rd.rgb + fd.rgb - 2.0 * rd.rgb * fd.rgb, max(rd.a, fd.a));
  float dmask = smoothstep(0.96, 0.99, comp.a);
  if (dmask > 0.001) {
    vec2 nn = comp.rg - 0.5;
    vec2 ruv = sv + vec2(-nn.x, nn.y) * (comp.b * 0.6 + 0.4) * uRefract;
    // \u96FE\u7B3A demo \u7528\u7684\u662F\u8F7B\u5EA6\u6A21\u7CCA\u7684\u80CC\u666F\u3001\u6F2B\u53CD\u5C04 0.2\u20130.5\u3001\u9AD8\u5149\u6307\u6570 48
    vec3 lens;
#if defined(NEBBIA_DEW) && NEBBIA_FOG_TEX
    // \u51DD\u9732\u7684\u7EC6\u6C34\u73E0\u53C8\u5BC6\u53C8\u5C0F\uFF0C\u6BCF\u4E2A\u50CF\u7D20\u91CD\u7B97\u4E00\u904D\u7A97\u5916\u592A\u8D35\uFF1A\u8BFB\u9884\u6E32\u67D3\u597D\u7684\u540C\u6837\u6A21\u7CCA\uFF080.006\uFF09\u7684\u7A97\u5916\uFF0C\u4F4E\u5206\u8FA8\u7387\u4E0B\u653E\u5927\u770B\u4E0D\u51FA\u5DEE\u522B\u3002
    // \u64E6\u75D5\u8FB9\u7684\u6C34\u73E0\uFF08rd \u5360\u4E0A\u98CE\uFF09\u7167\u65E7\u73B0\u7B97
    if (!uSeeThrough && fd.a > rd.a) lens = texture(uFogSharp, vec2(ruv.x - uSceneShift.x, 1.0 - ruv.y + uSceneShift.y)).rgb;
    else
#endif
    lens = uSeeThrough ? texture(uWallpaper,ruv).rgb : scene(vec2(ruv.x * aspect, ruv.y), 0.006);
    vec3 normal = normalize(vec3(nn * 2.0, 1.0));
    vec3 lightDir = normalize(vec3(-1.0, 1.0, 2.0));
    float lambert = clamp(dot(lightDir, normal), 0.0, 1.0);
    float spec = pow(max(dot(normal, normalize(lightDir + vec3(0.0, 0.0, 1.0))), 0.0), 48.0);
    lens += (lambert - 0.8) * vec3(0.3, 0.3, 0.32) + spec * vec3(0.3, 0.3, 0.33) * uGlint;
    if (uSeeThrough) {
      // \u548C raindrop-fx \u4E00\u6837\uFF0C\u6C34\u73E0\u662F\u4E00\u9897\u900F\u955C\uFF0C\u91CC\u9762\u662F\u6298\u5C04\u8FC7\u6765\u7684\u80CC\u666F\u3002\u900F\u660E\u65F6\u62FF\u4E0D\u5230\u7A97\u540E\u9762\u7684\u753B\u9762\uFF0C\u6298\u5C04\u7684\u8FD8\u662F\u96FE\u7B3A\u81EA\u5DF1\u7684
      // \u7A97\u5916\uFF08\u9009\u4E86\u6C34\u65CF\u7BB1\u6298\u5C04\u6C34\u65CF\u7BB1\uFF0C\u522B\u7684\u6298\u5C04\u96E8\u591C\u8857\u666F\uFF09\u3002\u767D\u96FE\u65F6\u6C34\u73E0\u4E0D\u900F\u660E\uFF1A\u8857\u666F\u6BD4\u96FE\u6697\uFF0C\u548C\u900F\u51FA\u540E\u9762\u7684\u5B57\u4E00\u6837\u662F\u6697\u7684\uFF1B
      // \u6DF1\u8272\u96FE\u65F6\u5B57\u900F\u51FA\u7684\u540E\u9762\u6BD4\u96FE\u4EAE\uFF0C\u6C34\u73E0\u4E5F\u8981\u4EE5\u900F\u51FA\u540E\u9762\u4E3A\u4E3B\uFF1A\u8857\u666F\u7559\u4E00\u534A\u4E0D\u5230\uFF0C\u8FB9\u4E0A\u4E00\u5708\u6298\u5C04\u7684\u6697\u8FB9
      float dark = 1.0 - dot(uMistTint, vec3(0.333));
      float rimA = (1.0 - smoothstep(0.975, 0.995, comp.a)) * 0.21;
      float da = mix(1.0, 0.47 + rimA, dark);
      lens = max(lens, vec3(0.0)) * mix(1.0, 0.47, dark);
      alpha = mix(alpha, da, dmask);
    } else {
      // \u8F66\u53A2\u91CC\u7684\u706F\u7167\u5728\u6C34\u73E0\u8868\u9762\uFF1A\u6BD4\u7EAF\u900F\u8FC7\u53BB\u7684\u7A97\u5916\u4EAE\u4E00\u70B9\uFF0C\u4E0D\u4F1A\u5728\u7070\u767D\u7684\u96FE\u4E0A\u663E\u6210\u9ED1\u70B9
      lens = max(lens, fog * 0.55) + haze * 0.08 * dens;
#ifdef NEBBIA_DEW
      // \u51DD\u9732\u73BB\u7483\u4E0A\u7684\u7EC6\u5C0F\u51B7\u51DD\u6C34\u73E0\uFF08nebbiaCondensation.ts\uFF09\uFF1A\u540E\u9762\u6697\uFF08\u6C34\u65CF\u7BB1\u7684\u6C34\u3001\u591C\u91CC\u7684\u8857\uFF09\u65F6\u7167\u5E38\u753B\u4F1A\u50CF\u4E00\u7C92\u7C92\u810F\u70B9\uFF1B
      // \u6309\u6302\u5728\u73BB\u7483\u4E0A\u7684\u6C34\u73E0\u753B\uFF0C\u6BD4\u96FE\u4EAE\u4E00\u70B9\u3001\u5E26\u4E00\u70B9\u9AD8\u5149\u3002\u64E6\u75D5\u8FB9\u7684\u6C34\u73E0\uFF08rd \u5360\u4E0A\u98CE\u7684\u5730\u65B9\uFF09\u7167\u65E7
      float fine = step(rd.a, fd.a);
      lens = mix(lens, max(lens * 1.15, fog * (1.0 + 0.051 * uDewGlint)) + (spec * 0.544 + lambert * 0.068) * dens * uDewGlint, fine);
#endif
    }
    col = mix(col, lens, dmask);
  }
#ifdef NEBBIA_SOFT_HIGHLIGHT
  // \u6C34\u65CF\u7BB1\uFF1A\u805A\u5149\u706F\u3001\u6C34\u6BCD\u3001\u51DD\u9732\u7684\u6C34\u73E0\u53E0\u5728\u540C\u4E00\u5904\u4F1A\u52A0\u8FC7 1\uFF0C\u70E7\u6210\u4E00\u5757\u767D\u3002\u6700\u4EAE\u7684\u90A3\u4E2A\u901A\u9053\u8FC7\u4E86 0.75 \u4EE5\u540E
  // \u6309\u6307\u6570\u6162\u6162\u903C\u8FD1 1\uFF0C\u4E09\u4E2A\u901A\u9053\u540C\u6BD4\u4F8B\u538B\uFF0C\u989C\u8272\u4E0D\u53D8\uFF1B0.75 \u4EE5\u4E0B\u4E0D\u52A8
  if (!uSeeThrough) {
    float pk = max(col.r, max(col.g, col.b));
    if (pk > 0.75) col *= (0.75 + 0.25 * (1.0 - exp((0.75 - pk) / 0.25))) / pk;
  }
#endif
  outColor = uSeeThrough ? vec4(max(col, 0.0), alpha) : vec4(col, 1.0);
}`,xo=Ue(!0),yo=Ue(!1),Xn=(o,e="rain")=>{let t=e==="dew";return o==="aquarium"?{fog:qn(St),main:Ue(!0,St,t),mainFallback:Ue(!1,St,t)}:t?{fog:Un,main:Ue(!0,Oe,!0),mainFallback:Ue(!1,Oe,!0)}:{fog:Un,main:xo,mainFallback:yo}},Kn=`#version 300 es
in vec2 aCorner; in vec4 aRect; in vec2 aParam;
uniform vec2 uView;
out vec2 vUV; out float vSize; out float vAlpha;
void main() {
  vec2 p = aRect.xy + (aCorner - 0.5) * aRect.zw;
  vUV = aCorner; vSize = aParam.x; vAlpha = aParam.y;
  gl_Position = vec4(p.x / uView.x * 2.0 - 1.0, p.y / uView.y * 2.0 - 1.0, 0.0, 1.0);
}`,jn=`#version 300 es
precision mediump float;
uniform sampler2D uSprite;
in vec2 vUV; in float vSize; in float vAlpha; out vec4 o;
void main() {
  vec4 c = texture(uSprite, vUV);
  c.a *= vAlpha;
  o = vec4(c.rg * c.a, vSize * c.a, c.a);
}`;var Me=Math.PI*2,I=(o,e)=>o+Math.random()*(e-o),Mt=(o,e,t)=>{let r=Math.max(0,Math.min(1,(t-o)/(e-o)));return r*r*(3-2*r)},Yn=()=>{let o=document.createElement("canvas");return o.width=o.height=64,o},go=o=>{let e=[],t=[],r=[],i=I(.13,.18),n=l=>{let c=[];for(let u=I(.2,.4);u<l-.08;u+=I(.14,.28))Math.random()<.3||c.push({at:u,len:I(.1,.3)*(1-u),ang:I(.85,1.2),side:Math.random()<.25?Math.random()<.5?1:-1:0});return c};if(o==="bead"){t.push({x:I(-.05,.05),y:I(-.05,.05),r:I(.45,.55),a:.95});for(let l=0,c=4+Math.floor(Math.random()*3);l<c;l+=1){let u=Math.random()*Me,h=I(.15,.32);t.push({x:Math.cos(u)*h,y:Math.sin(u)*h,r:I(.18,.3),a:I(.55,.8)})}for(let l=0,c=5+Math.floor(Math.random()*3);l<c;l+=1)e.push({ang:Math.random()*Me,len:I(.45,.75),w:I(.04,.07),branches:[]})}else if(o==="clump"){let l=0,c=0,u=Math.random()*Me;for(let h=0,m=7+Math.floor(Math.random()*10);h<m;h+=1){t.push({x:l,y:c,r:I(.1,.22),a:I(.6,.95)}),u+=I(-1.6,1.6);let f=I(.07,.15);l=Math.max(-.42,Math.min(.42,l+Math.cos(u)*f)),c=Math.max(-.42,Math.min(.42,c+Math.sin(u)*f))}}else{t.push({x:0,y:0,r:I(.1,.22),a:.9});let l=I(.7,1),c=new Set;Math.random()<.5&&c.add(Math.floor(Math.random()*6)),c.size&&Math.random()<.3&&c.add(Math.floor(Math.random()*6));for(let u=0;u<6;u+=1){if(c.has(u)&&Math.random()<.4)continue;let h=l*I(.86,1)*(c.has(u)?I(.3,.6):1);e.push({ang:u*Me/6+I(-.05,.05),len:h,w:i*I(.9,1.1),branches:n(h)})}Math.random()<.5&&t.push({x:I(-.2,.2),y:I(-.2,.2),r:I(.12,.25),a:.6})}for(let l=0,c=6+Math.floor(Math.random()*14);l<c;l+=1)if(e.length&&Math.random()<.6){let u=e[Math.floor(Math.random()*e.length)],h=Math.random()*u.len;r.push({x:Math.cos(u.ang)*h+I(-.04,.04),y:Math.sin(u.ang)*h+I(-.04,.04),r:I(.025,.06)})}else{let u=t[Math.floor(Math.random()*t.length)],h=Math.random()*Me,m=Math.random()*u.r;r.push({x:u.x+Math.cos(h)*m,y:u.y+Math.sin(h)*m,r:I(.03,.07)})}let a=t.slice().sort((l,c)=>c.r-l.r).slice(0,3).map(l=>({x:l.x,y:l.y,w:l.r*l.r})),s=a.reduce((l,c)=>l+c.w,0);for(let l of a)l.w/=s;return{arms:e,discs:t,specks:r,lumps:a}},$n=(o,e)=>{let t=Yn(),r=t.getContext("2d"),i=26.88;r.translate(64/2,64/2),r.lineCap="round";for(let c of o.discs){let u=r.createRadialGradient(c.x*i,c.y*i,0,c.x*i,c.y*i,c.r*i);u.addColorStop(0,`rgba(255,255,255,${c.a})`),u.addColorStop(.6,`rgba(255,255,255,${c.a*.6})`),u.addColorStop(1,"rgba(255,255,255,0)"),r.fillStyle=u,r.beginPath(),r.arc(c.x*i,c.y*i,c.r*i,0,Me),r.fill()}for(let c of o.arms){r.save(),r.rotate(c.ang),r.strokeStyle="rgba(255,255,255,0.55)",r.lineWidth=c.w*i,r.beginPath(),r.moveTo(0,0),r.lineTo(c.len*i,0),r.stroke(),r.strokeStyle="rgba(255,255,255,0.42)",r.lineWidth=c.w*i*.75;for(let u of c.branches)for(let h of[-1,1])u.side&&u.side!==h||(r.beginPath(),r.moveTo(u.at*i,0),r.lineTo((u.at+Math.cos(u.ang)*u.len)*i,h*Math.sin(u.ang)*u.len*i),r.stroke());r.restore()}r.fillStyle="rgba(255,255,255,0.95)";for(let c of o.specks)r.beginPath(),r.arc(c.x*i,c.y*i,c.r*i,0,Me),r.fill();let n=Yn(),a=n.getContext("2d");a.filter=`blur(${e}px)`,a.drawImage(t,0,0);let s=a.getImageData(0,0,64,64).data,l=new Uint8ClampedArray(4096);for(let c=0;c<l.length;c+=1)l[c]=s[c*4+3];return l},yr=(o,e)=>{let t=go(o),r=$n(t,.35),i=$n(t,2),n=[228,236,250],a=[196,212,236],s=[16,20,30],l=[];for(let c=0;c<16;c+=1){let u=c/15,h=u*.85,m=new ImageData(64,64),f=m.data;for(let b=0;b<r.length;b+=1){let p=r[b]/255,y=i[b]/255,g=Mt(h,h+.3,p),d=g*(.88-.45*u),w=u>0?Mt(h-.2,h,p)*Mt(.04,.12,p)*(1-g)*.32*Math.min(1,u*2):0,x=Math.min(1,y*2)*(1-g)*.2*(1-u),E=d+w;if(E+x<.004)continue;let v=Mt(.8,1,p)*(1-u),A=E+x*(1-E);for(let M=0;M<3;M+=1){let T=n[M]+(255-n[M])*v,F=E>0?(T*d+a[M]*w)/E:0;f[b*4+M]=(F*E+s[M]*x*(1-E))/A}f[b*4+3]=A*255}l.push(m)}return{type:o,row:e,stages:l,lumps:t.lumps}},Jn=o=>{let e=document.createElement("canvas"),t=e.getContext("2d");return e.width=1024,e.height=64*o.length,o.forEach((r,i)=>r.stages.forEach((n,a)=>t.putImageData(n,a*64,i*64))),e};var Qn=`
// glow\uFF1A\u96FE\u7684\u5149\u6655\u5C42\uFF1Bfu\uFF1A\u5C4F\u5E55 uv\uFF08y \u5411\u4E0A\uFF09\uFF1Bn\uFF1A\u51B0\u9762\u659C\u9762\uFF08\u5C4F\u5E55\u65B9\u5411\uFF0Cy \u5411\u4E0A\uFF09\uFF1Bseed\uFF1A\u8FD9\u5757\u51B0\u9762\u7684\u968F\u673A\u6570\uFF08\u5B9A\u5B83\u7684\u6298\u5C04\u89D2\uFF09
vec3 fixedFire(sampler2D glow, vec2 fu, vec2 n, float seed) {
  const vec3 W = vec3(0.3, 0.59, 0.11);
  vec2 e = vec2(0.025, 0.0);
  vec2 lg = vec2(dot(textureLod(glow, fu + e.xy, 0.0).rgb - textureLod(glow, fu - e.xy, 0.0).rgb, W),
                 dot(textureLod(glow, fu + e.yx, 0.0).rgb - textureLod(glow, fu - e.yx, 0.0).rgb, W));
  float ll = length(lg), nl = length(n);
  if (ll < 1e-4 || nl < 1e-4) return vec3(0.0);
  float x = (dot(n / nl, lg / ll) - mix(0.6, 0.95, seed)) / 0.05;
  if (abs(x) >= 1.0) return vec3(0.0);
  float h = (x * 0.5 + 0.5) * 0.75;
  vec3 spec = clamp(abs(fract(h + vec3(0.0, 0.667, 0.333)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
  return spec * (1.0 - x * x) * smoothstep(0.002, 0.03, ll);
}
`;var Q=10,vo="9.0",wo=2.6,Zn=`#version 300 es
in vec2 aCorner; in vec4 aA; in vec4 aB; in vec2 aC;
uniform vec2 uView, uShift;
out vec2 vLocal; out vec2 vHalo; out vec2 vFu; flat out vec4 vB; flat out float vR; flat out float vSeed;
void main() {
  vec2 c = (aCorner * 2.0 - 1.0) * ${wo.toFixed(2)};
  float cs = cos(aA.w), sn = sin(aA.w);
  vec2 off = c * aA.z;
  vec2 p = aA.xy + vec2(off.x * cs - off.y * sn, off.x * sn + off.y * cs) + uShift;
  // vLocal\uFF1A\u8D34\u56FE\u91CC\u7684\u5750\u6807\uFF08\u5355\u4F4D\u662F\u96EA\u82B1\u534A\u5F84\uFF0C\u538B\u6241\u4E4B\u524D\uFF09\uFF1BvHalo\uFF1A\u7CCA\u5149\u3001\u67D4\u5149\u7528\u7684\uFF08\u4E0D\u538B\u6241\uFF09
  vLocal = c / aC; vHalo = c; vB = aB; vR = aA.z;
  // \u6BCF\u7247\u96EA\u81EA\u5DF1\u7684\u968F\u673A\u6570\uFF08\u95EA\u5149\u7528\uFF09\uFF1A\u8D34\u4E0A\u4EE5\u540E\u8F6C\u89D2\u3001\u79CD\u7C7B\u90FD\u4E0D\u518D\u53D8
  vSeed = fract(sin(aA.w * 12.9898 + aB.y * 78.233) * 43758.5453);
  // \u96FE\u7684\u9884\u6E32\u67D3\u7EB9\u7406\u7B2C 0 \u884C\u662F\u5C4F\u5E55\u5E95\u90E8
  vFu = vec2(p.x / uView.x, 1.0 - p.y / uView.y);
  gl_Position = vec4(p.x / uView.x * 2.0 - 1.0, 1.0 - p.y / uView.y * 2.0, 0.0, 1.0);
}`,ei=`#version 300 es
precision mediump float;
uniform sampler2D uAtlas, uGlow, uBehind;
uniform bool uLit; uniform vec2 uGrid; uniform highp vec2 uView;
in vec2 vLocal; in vec2 vHalo; in vec2 vFu; flat in vec4 vB; flat in float vR; flat in float vSeed;
out vec4 o;
${Qn}
vec4 tile(float col, vec2 t, float bias) { return texture(uAtlas, (vec2(col, vB.y) + t) / uGrid, bias); }
void main() {
  float p = vB.w;
  vec2 t = 0.5 + vLocal * ${(.84/2).toFixed(3)};
  // \u76F8\u90BB\u4E24\u4E2A\u878D\u5316\u9636\u6BB5\u4EA4\u53C9\uFF1B\u8FD8\u6CA1\u98D8\u5230\u7126\u70B9\u4E0A\u7684\u53D6\u66F4\u7CCA\u7684\u4E00\u7EA7\u3002\u4E0D\u653E\u5728\u5206\u652F\u91CC\u53D6\uFF08\u5206\u652F\u91CC\u53D6\u6837\u7684 mipmap \u7EA7\u522B\u4E0D\u53EF\u9760\uFF09\uFF0C\u683C\u5B50\u5916\u9762\u4E58 0
  float st = floor(vB.x), fr = vB.x - st, bias = 2.0 * (1.0 - p);
  vec2 tc = clamp(t, 0.0, 1.0);
  vec4 c = mix(tile(st, tc, bias), tile(min(st + 1.0, uGrid.x - 1.0), tc, bias), fr);
  c *= vB.z * float(t == tc);
  // \u8EAB\u540E\u7684\u5149\uFF1A\u989C\u8272\u5E26\u5927\u534A\uFF08\u8FD8\u770B\u5F97\u51FA\u662F\u767D\u96EA\uFF09\uFF0C\u4EAE\u5EA6\u8DDF\u7740\u8D70\uFF1A\u6697\u5904\u538B\u5F97\u5F88\u6697\uFF0C\u706F\u4E0B\u4EAE\u8FC7\u96FE
  vec3 L = uLit ? texture(uGlow, vFu).rgb : vec3(0.42);
  float lum = dot(L, vec3(0.3, 0.59, 0.11));
  vec3 hue = clamp(mix(vec3(1.0), L / max(lum, 0.02), 0.8), 0.0, 2.2);
  vec3 lit = hue * clamp(0.22 + 2.5 * lum, 0.0, 1.4);
  vec3 rgb = c.rgb * lit;
  // \u9006\u5149\uFF08\u8EAB\u540E\u6709\u4EAE\u5149\uFF09\uFF1A\u96EA\u50CF\u51B0\u4E00\u6837\u88AB\u7167\u900F\u3002\u51B0\u6676\u7684\u539A\u8584\uFF08\u8D34\u56FE\u7684\u4E0D\u900F\u660E\u5EA6\uFF09\u5F53\u6210\u8868\u9762\u7684\u8D77\u4F0F\uFF0C\u659C\u7684\u5730\u65B9\u628A\u8EAB\u540E\u7684\u7A97\u5916\u5F80\u91CC\u6298\uFF08\u53D6\u4E3B\u753B\u9762\u90A3\u5C42\u7A0D\u7CCA\u7684\u7A97\u5916\uFF0CuBehind\uFF09\uFF0C
  // \u900F\u8FC7\u6765\u7684\u5149\u6BD4\u56DB\u5468\u4EAE\u4E00\u70B9\uFF1B\u8584\u7684\u8FB9\u3001\u659C\u9762\u4E0A\u518D\u4EAE\u4E00\u9053\u3002\u6697\u5904\u7167\u65E7\u662F\u767D\u96EA\u3002\u53EA\u5728\u5BF9\u4E0A\u7126\uFF08\u8D34\u4E0A\u73BB\u7483\uFF09\u540E\uFF0C\u5316\u5F97\u8D8A\u591A\u8D8A\u5F31\uFF08\u5316\u6210\u6C34\u4EA4\u7ED9\u6C34\u73E0\u53BB\u6298\u5C04\uFF09
  float back = float(uLit) * smoothstep(0.08, 0.45, lum) * p * (1.0 - 0.7 * vB.x / ${15 .toFixed(1)});
  float a0 = c.a / max(vB.z, 0.001);
  vec2 n = vec2(dFdx(a0), dFdy(a0)) * vR;
  float edge = clamp(length(n) * 0.5, 0.0, 1.0);
  // \u8272\u6563\uFF08\u6BD4\u771F\u51B0\u653E\u5927\u4E86\u4E0D\u5C11\uFF0C\u8981\u770B\u5F97\u51FA\uFF09\uFF1A\u7EA2\u3001\u7EFF\u3001\u84DD\u6298\u5F97\u4E0D\u4E00\u6837\u591A\uFF0C\u68F1\u4E0A\u518D\u6CBF\u659C\u7684\u65B9\u5411\u5206\u5F00\u4E00\u70B9\uFF0C\u900F\u8FC7\u6765\u7684\u7A97\u5916\u5728\u8FB9\u4E0A\u9519\u51FA\u5F69\u8FB9\u3001\u989C\u8272\u52A0\u6D53\uFF1B
  vec2 bend = n * vR * 0.7 / uView, split = n / max(length(n), 0.001) * vR * 0.35 * edge / uView;
  vec3 through = vec3(texture(uBehind, vFu - bend * 0.8 - split).r, texture(uBehind, vFu - bend).g, texture(uBehind, vFu - bend * 1.25 + split).b);
  vec3 gray = vec3(dot(through, vec3(0.3333)));
  through = gray + (through - gray) * (1.0 + 1.5 * edge);
  vec3 ice = (through * (1.1 + 0.3 * edge) + min(L, vec3(1.2)) * edge * 0.3) * c.a + c.rgb * lit * 0.15;
  rgb = mix(rgb, ice, back * 0.85);
  float d = length(vHalo);
  // \u8FD8\u6CA1\u98D8\u5230\u7126\u70B9\u4E0A\uFF1A\u5916\u9762\u5957\u4E00\u5708\u7CCA\u7684
  float ha = 0.35 * (1.0 - p) * max(0.0, 1.0 - d / (1.6 - 0.6 * p));
  rgb = vec3(0.92, 0.95, 1.0) * lit * ha + rgb * (1.0 - ha);
  float a = ha + c.a * (1.0 - ha);
  // \u9760\u8FD1\u4EAE\u5149\uFF1A\u5916\u9762\u6563\u4E00\u5708\u5F88\u6DE1\u7684\u67D4\u5149\uFF08\u53EA\u52A0\u989C\u8272\uFF0C\u6BD4\u96EA\u82B1\u5927\u4E0D\u4E86\u591A\u5C11\uFF09\uFF0C\u5316\u5F97\u8D8A\u591A\u8D8A\u6DE1\uFF1B\u6CA1\u6709\u5149\u7167\uFF08\u900F\u660E\uFF09\u65F6\u4E0D\u52A0
  float soft = float(uLit) * smoothstep(0.12, 0.6, lum) * 0.45 * exp(-d * d * 2.4) * vB.z * (1.0 - vB.x / ${15 .toFixed(1)});
  // \u706B\u5F69\uFF08\u8272\u6563\uFF09\uFF1A\u8D34\u56FE\u6309\u683C\u5B50\u5206\uFF0C\u6BCF\u683C\u4E00\u5757\u51B0\u9762\u3001\u6709\u81EA\u5DF1\u56FA\u5B9A\u7684\u6298\u5C04\u89D2\uFF0C\u53EA\u6709\u51B0\u68F1\u4E0A\uFF08\u659C\u9762\uFF09\u7684\u683C\u5B50\u4F1A\u51FA\uFF1B\u989C\u8272\u7531\u659C\u9762\u548C\u5149\u7684\u65B9\u5411\u5B9A\uFF08nebbiaFireGlsl.ts\uFF09\uFF0C
  // \u53EA\u52A0\u5149\uFF0C\u4EAE\u5EA6\u6309\u8EAB\u540E\u5149\u7684\u4EAE\u5EA6\u7ED9\uFF0C\u4E0D\u8DDF\u706F\u7684\u989C\u8272\u8D70\uFF1B\u7EA6\u56DB\u6210\u683C\u5B50\u6709\u671D\u5F97\u5BF9\u7684\u51B0\u9762
  vec2 g = tc * ${vo}, gc = floor(g), gf = fract(g) - 0.5;
  float h = fract(sin(dot(gc, vec2(12.9898, 78.233)) + vSeed * 91.7) * 43758.5453);
  vec3 fire = fixedFire(uGlow, vFu, n, fract(h * 7.31)) * step(h, 0.4);
  float glint = back * exp(-dot(gf, gf) * 18.0) * edge * (0.6 + 2.0 * lum) * vB.z;
  o = vec4(rgb + min(L, vec3(1.2)) * soft + fire * glint * 2.2, a);
}`,Tt=class{constructor(e,t){this.gl=e;this.prog=null;this.uView=null;this.uShift=null;this.uGrid=null;this.rows=0;this.capacity=0;this.atlas=J(e,t),this.vao=e.createVertexArray(),e.bindVertexArray(this.vao),e.bindBuffer(e.ARRAY_BUFFER,e.createBuffer()),e.bufferData(e.ARRAY_BUFFER,new Float32Array([0,0,1,0,0,1,1,1]),e.STATIC_DRAW),e.enableVertexAttribArray(0),e.vertexAttribPointer(0,2,e.FLOAT,!1,0,0),this.buf=e.createBuffer(),e.bindBuffer(e.ARRAY_BUFFER,this.buf);let r=Q*4;e.enableVertexAttribArray(1),e.vertexAttribPointer(1,4,e.FLOAT,!1,r,0),e.vertexAttribDivisor(1,1),e.enableVertexAttribArray(2),e.vertexAttribPointer(2,4,e.FLOAT,!1,r,16),e.vertexAttribDivisor(2,1),e.enableVertexAttribArray(3),e.vertexAttribPointer(3,2,e.FLOAT,!1,r,32),e.vertexAttribDivisor(3,1),e.bindVertexArray(null)}attach(e,t,r,i){let{gl:n}=this;this.prog=e,n.useProgram(e),this.uView=n.getUniformLocation(e,"uView"),this.uShift=n.getUniformLocation(e,"uShift"),this.uGrid=n.getUniformLocation(e,"uGrid"),n.uniform1i(n.getUniformLocation(e,"uAtlas"),this.atlas.unit),n.uniform1i(n.getUniformLocation(e,"uGlow"),t),n.uniform1i(n.getUniformLocation(e,"uBehind"),r),n.uniform1i(n.getUniformLocation(e,"uLit"),i?1:0)}setAtlas(e,t){let{gl:r}=this;this.rows=t,r.activeTexture(r.TEXTURE0+this.atlas.unit),r.bindTexture(r.TEXTURE_2D,this.atlas.t),r.pixelStorei(r.UNPACK_PREMULTIPLY_ALPHA_WEBGL,!0),r.texImage2D(r.TEXTURE_2D,0,r.RGBA,r.RGBA,r.UNSIGNED_BYTE,e),r.pixelStorei(r.UNPACK_PREMULTIPLY_ALPHA_WEBGL,!1),r.generateMipmap(r.TEXTURE_2D),r.texParameteri(r.TEXTURE_2D,r.TEXTURE_MIN_FILTER,r.LINEAR_MIPMAP_LINEAR),r.texParameteri(r.TEXTURE_2D,r.TEXTURE_MAX_LEVEL,4)}draw(e,t,r,i,n){let{gl:a}=this;!this.prog||!this.rows||!t||(a.useProgram(this.prog),a.bindVertexArray(this.vao),a.bindBuffer(a.ARRAY_BUFFER,this.buf),e.length>this.capacity&&(a.bufferData(a.ARRAY_BUFFER,e.byteLength,a.DYNAMIC_DRAW),this.capacity=e.length),a.bufferSubData(a.ARRAY_BUFFER,0,e,0,t*Q),a.uniform2f(this.uView,r,i),a.uniform2f(this.uShift,n[0],n[1]),a.uniform2f(this.uGrid,16,this.rows),a.enable(a.BLEND),a.blendFunc(a.ONE,a.ONE_MINUS_SRC_ALPHA),a.drawArraysInstanced(a.TRIANGLE_STRIP,0,4,t),a.disable(a.BLEND),a.bindVertexArray(null))}};var We=12,Te={count:100,size:1,halo:2,speed:1,glow:1},ve=Math.PI*2,Rt=.4,Eo=4.5*(7.5-1+Rt+.05),ue=(o,e)=>o+Math.random()*(e-o),vr=o=>o*o*(3-2*o),Ao=(o,e,t)=>o+Math.atan2(Math.sin(e-o),Math.cos(e-o))*t,Ft=class{constructor(){this.flies=[];this.out=new Float32Array(0);this.W=0;this.H=0;this.k=1;this.spawnAcc=0;this.palette=[[1,.81,.54],[.54,.8,1],[.8,.54,1],[1,.68,.32],[1,.95,.85]]}setPalette(e,t,r){let i=[e,t,r].map(n=>{let a=Math.max(n[0],n[1],n[2],.05);return n.map(s=>s/a)});this.palette=[...i,...this.palette.slice(3)]}resize(e,t){if(e===this.W&&t===this.H)return;let r=!this.W,i=e/(this.W||e),n=t/(this.H||t);if(this.W=e,this.H=t,this.k=t/1e3,r){let a=Te,s=Math.round(a.count*e*t/(1280*720));this.flies=Array.from({length:s},()=>{let l=this.spawn();return l.age=Math.random()*(l.app+l.hold+l.leave),l.px=l.py=NaN,l});return}for(let a of this.flies)a.x*=i,a.y*=n,a.px*=i,a.py*=n}spawn(){let{k:e}=this,t=Te.speed||1,r=Math.random()*ve,i=ue(80,220)*e,n=Math.random()*this.W,a=Math.random()*this.H,s=Math.cos(r)*i,l=Math.sin(r)*i-ue(0,50)*e,c=ue(1.5,3)/t,u=ue(3,6),h=5+Math.floor(Math.random()*6);return{x:n,y:a,r:(Math.random()<.3?ue(4.5,6):ue(2.5,4.5))*e*Te.size,app:c,ox:s,oy:l,zig:ue(12,30)*e,zf:ue(1.2,2.2),fp:Math.random()*ve,hold:u*(h-1+Rt+.05),leave:ue(.8,1.4)/t,dir:Math.random()*ve,far:ue(120,260)*e,age:0,crawl:Math.random()*ve,blink:u,bp:(1-c/u%1)%1,tint:Math.floor(Math.random()*5),px:n+s,py:a+l,head:Math.atan2(-l,-s)}}step(e){let t=Te,{W:r,H:i}=this;if(!r||!i)return[this.out,0];let n=t.count*r*i/(1280*720),a=3.35/(t.speed||1)+Eo;for(this.spawnAcc+=e*n/a;this.spawnAcc>=1;this.spawnAcc-=1)this.flies.push(this.spawn());let s=this.flies,l=0;for(let c of s)(c.age+=e)<c.app+c.hold+c.leave&&(s[l++]=c);s.length=l,this.out.length<l*We&&(this.out=new Float32Array(Math.ceil(l*1.5)*We));for(let c=0;c<l;c+=1)this.pack(s[c],c*We,e);return[this.out,l]}pack(e,t,r){let{k:i}=this,n=e.x,a=e.y,s=1,l=.92,c=1,u=0,h=Math.min(1,e.age/e.app),m=(e.age-e.app-e.hold)/e.leave;if(h<1){let w=ve*2.5,x=S(h+.85*(Math.sin(w*h+e.fp)-Math.sin(e.fp)*(1-h)-Math.sin(w+e.fp)*h)/w),E=(1-x)**2,v=Math.hypot(e.ox,e.oy)||1,A=-e.oy/v,M=e.ox/v,T=e.zig*(1-x)*(Math.sin(e.age*e.zf*ve+e.fp)+.5*Math.sin(e.age*e.zf*2.7*ve+e.fp*2));n+=e.ox*E+A*T,a+=e.oy*E+M*T,s=.7+.3*h,l*=.45+.55*h,c=h,u=(.6+.4*Math.abs(Math.sin(e.age*47+e.fp)))*(1-h**4)}else if(m<0){let w=e.age-e.app,x=vr(S((Math.sin(w*.9+e.fp)-.2)/.4));e.crawl+=(Math.random()-.5)*.78*Math.sqrt(r),e.x+=Math.cos(e.crawl)*7*i*x*r,e.y+=Math.sin(e.crawl)*7*i*x*r,n=e.x,a=e.y,u=.35*Math.max(0,Math.sin(w*1.7+e.fp*3)-.94)/.06*Math.abs(Math.sin(w*40))}else{let w=m*m*e.far,x=e.zig*.6*m*Math.sin(e.age*e.zf*ve+e.fp);n+=Math.cos(e.dir)*w-Math.sin(e.dir)*x,a+=Math.sin(e.dir)*w+Math.cos(e.dir)*x,s=1-.3*m,c=1-m,l*=1-m**1.5,u=.6+.4*Math.abs(Math.sin(e.age*47+e.fp))}let f=n-e.px,b=a-e.py;f*f+b*b>.0025*i*i&&(e.head=Ao(e.head,Math.atan2(b,f),Math.min(1,r*10))),e.px=n,e.py=a;let p=(e.age/e.blink+e.bp)%1,y=p<.15?vr(p/.15):p<Rt?1-vr((p-.15)/(Rt-.15)):0,g=this.out;g[t]=n,g[t+1]=a,g[t+2]=e.r*s,g[t+3]=l,g[t+4]=c,g[t+5]=m<0?1:1-m,g[t+6]=e.head,g[t+7]=u,g[t+8]=y*Te.glow*(m<0?1:1-m);let d=this.palette[e.tint];g[t+9]=d[0],g[t+10]=d[1],g[t+11]=d[2]}};var ti=`#version 300 es
in vec2 aCorner; in vec4 aA; in vec4 aB; in vec4 aC;
uniform vec2 uView; uniform float uHalo;
out vec2 vLocal; out vec2 vFu; flat out vec4 vP; flat out vec4 vGlow;
void main() {
  // \u65B9\u5757\u8981\u76D6\u4F4F\u7CCA\u5F00\u7684\u866B\u5B50\u548C\u67D4\u5149\uFF1A\u67D4\u5149\u5230\u65B9\u5757\u8FB9\u4E0A\u8981\u6DE1\u5230\u770B\u4E0D\u51FA\uFF08\u4F4E\u4E8E 1/255\uFF09\uFF0C\u4E0D\u7136\u706F\u4E0B\u770B\u5F97\u89C1\u65B9\u6846
  float ext = max(2.6, 4.1 * uHalo);
  vec2 c = (aCorner * 2.0 - 1.0) * ext;
  // \u6309\u8EAB\u5B50\u671D\u5411\u8F6C\uFF1AvLocal \u662F\u866B\u5B50\u81EA\u5DF1\u7684\u5750\u6807\uFF08x \u671D\u5934\uFF0C\u5355\u4F4D\u662F\u534A\u5F84\uFF09
  float cs = cos(aB.z), sn = sin(aB.z);
  vec2 off = c * aA.z;
  vec2 p = aA.xy + vec2(off.x * cs - off.y * sn, off.x * sn + off.y * cs);
  vLocal = c; vP = vec4(aA.w, aB.x, aB.y, aB.w); vGlow = aC;
  // \u96FE\u7684\u9884\u6E32\u67D3\u7EB9\u7406\u7B2C 0 \u884C\u662F\u5C4F\u5E55\u5E95\u90E8
  vFu = vec2(p.x / uView.x, 1.0 - p.y / uView.y);
  gl_Position = vec4(p.x / uView.x * 2.0 - 1.0, 1.0 - p.y / uView.y * 2.0, 0.0, 1.0);
}`,ri=`#version 300 es
precision mediump float;
uniform sampler2D uGlow;
// uHalo \u4E24\u8FB9\u90FD\u7528\uFF1A\u548C\u9876\u70B9\u7740\u8272\u5668\u7684\u7CBE\u5EA6\u8981\u4E00\u6837
uniform bool uLit; uniform highp float uHalo;
in vec2 vLocal; in vec2 vFu; flat in vec4 vP; flat in vec4 vGlow;
out vec4 o;
// \u4E00\u4E2A\u692D\u5706\uFF08\u4E2D\u5FC3 c\u3001\u534A\u8F74 ab\u3001\u8F6C a \u5F27\u5EA6\uFF09\u7684\u8986\u76D6\uFF0C\u8FB9\u7F18\u5BBD e
float blob(vec2 q, vec2 c, vec2 ab, float a, float e) {
  q -= c;
  float cs = cos(a), sn = sin(a);
  q = vec2(q.x * cs + q.y * sn, -q.x * sn + q.y * cs) / ab;
  return 1.0 - smoothstep(1.0 - e, 1.0 + e, length(q));
}
void main() {
  float p = vP.y, flap = vP.w;
  vec2 q = vLocal;
  // \u6CA1\u5BF9\u4E0A\u7126\uFF1A\u8FB9\u7F18\u7CCA\u5F00
  float e = 0.18 + 0.9 * (1.0 - p);
  // \u8EAB\u5B50\uFF1A\u5934\u3001\u80F8\u3001\u957F\u4E00\u70B9\u7684\u809A\u5B50
  float body = max(max(blob(q, vec2(0.62, 0.0), vec2(0.17, 0.15), 0.0, e),
                       blob(q, vec2(0.3, 0.0), vec2(0.22, 0.2), 0.0, e)),
                   blob(q, vec2(-0.25, 0.0), vec2(0.48, 0.19), 0.0, e));
  // \u7FC5\u8180\uFF1A\u6536\u62E2\u65F6\u76D6\u5728\u80CC\u4E0A\u5F80\u540E\uFF0C\u5F20\u5F00\u65F6\u5F80\u4E24\u8FB9\u6446\uFF1B\u6247\u5F97\u5FEB\uFF0C\u770B\u7740\u662F\u4E00\u7247\u534A\u900F\u660E\u7684\u7CCA\u5F71
  float wa = 0.12 + 1.2 * flap, we = e + 0.3 * flap;
  float wing = max(blob(q, vec2(-0.15 + 0.2 * flap, 0.08 + 0.45 * flap), vec2(0.62, 0.17), wa, we),
                   blob(q, vec2(-0.15 + 0.2 * flap, -0.08 - 0.45 * flap), vec2(0.62, 0.17), -wa, we));
  // \u8EAB\u540E\u7684\u5149\uFF1A\u989C\u8272\u5E26\u5927\u534A\uFF0C\u4EAE\u5EA6\u8DDF\u7740\u8D70\uFF1A\u6697\u5904\u538B\u5F97\u5F88\u6697\uFF0C\u706F\u4E0B\u4EAE\u8FC7\u96FE
  vec3 L = uLit ? texture(uGlow, vFu).rgb : vec3(0.42);
  float lum = dot(L, vec3(0.3, 0.59, 0.11));
  vec3 hue = clamp(mix(vec3(1.0), L / max(lum, 0.02), 0.8), 0.0, 2.2);
  vec3 lit = hue * clamp(0.22 + 2.5 * lum, 0.0, 1.4);
  // \u8EAB\u5B50\u6DE1\u6DE1\u7684\u767D\uFF1B\u7FC5\u8180\u66F4\u8584\u3001\u900F\u5149\uFF0C\u53E0\u5728\u8EAB\u5B50\u4E0A\u9762
  float ab = body * 0.5, aw = wing * (0.3 - 0.1 * flap);
  float ca = (aw + ab * (1.0 - aw)) * vP.x;
  vec3 rgb = (vec3(0.95, 0.97, 1.0) * aw + vec3(0.86, 0.88, 0.92) * ab * (1.0 - aw)) * lit * vP.x;
  float d = length(q);
  // \u8FD8\u6CA1\u98D8\u5230\u7126\u70B9\u4E0A\uFF1A\u5916\u9762\u5957\u4E00\u5708\u7CCA\u7684
  float ha = 0.35 * (1.0 - p) * max(0.0, 1.0 - d / (1.6 - 0.6 * p)) * vP.x;
  rgb = vec3(0.92, 0.95, 1.0) * lit * ha + rgb * (1.0 - ha);
  float a = ha + ca * (1.0 - ha);
  // \u9760\u8FD1\u4EAE\u5149\uFF1A\u6563\u4E00\u5708\u5F88\u6DE1\u7684\u67D4\u5149\uFF08\u53EA\u52A0\u989C\u8272\uFF09\uFF1B\u6CA1\u6709\u5149\u7167\uFF08\u900F\u660E\uFF09\u65F6\u4E0D\u52A0
  // \u8EAB\u540E\u5DF2\u7ECF\u5F88\u4EAE\uFF08\u805A\u5149\u706F\u6B63\u7167\u7740\uFF09\u65F6\u67D4\u5149\u6536\u6389\u5927\u534A\uFF1A\u518D\u5F80\u4EAE\u5904\u52A0\u5149\u53EA\u4F1A\u53E0\u6210\u4E00\u7247\u767D
  float soft = float(uLit) * smoothstep(0.12, 0.6, lum) * (1.0 - 0.75 * smoothstep(0.6, 1.2, lum)) * 0.45 * exp(-d * d * 0.3 / (uHalo * uHalo)) * vP.z;
  // \u5C3E\u5DF4\u7684\u5149\uFF1A\u809A\u5B50\u5C16\u4E0A\u4E00\u5C0F\u56E2\uFF0C\u5916\u9762\u4E00\u5C0F\u5708\u5149\u6655\uFF0C\u53EA\u52A0\u989C\u8272\uFF08\u81EA\u5DF1\u53D1\u5149\uFF0C\u4E0D\u6321\u540E\u9762\uFF09\uFF1B\u6CA1\u5BF9\u4E0A\u7126\u65F6\u7CCA\u5927
  float dt = length(q - vec2(-0.55, 0.0)), gr = 0.45 + 0.35 * (1.0 - p);
  float lamp = (1.0 - smoothstep(0.0, gr, dt)) + 0.8 * exp(-dt * dt * 0.7);
  // \u4E2D\u5FC3\u504F\u767D\u4E00\u70B9\uFF08\u4EAE\u5230\u53D1\u767D\uFF09\uFF0C\u5916\u5708\u662F\u706F\u5149\u8272
  vec3 tint = mix(vGlow.yzw, vec3(1.0), 0.35 * (1.0 - smoothstep(0.0, gr, dt)));
  vec3 glow = tint * lamp * vGlow.x * vP.x * mix(0.5, 1.0, p);
  o = vec4(rgb + min(L, vec3(1.2)) * soft + glow, a);
}`,Lt=class{constructor(e){this.gl=e;this.prog=null;this.uView=null;this.uHalo=null;this.vao=e.createVertexArray(),e.bindVertexArray(this.vao),e.bindBuffer(e.ARRAY_BUFFER,e.createBuffer()),e.bufferData(e.ARRAY_BUFFER,new Float32Array([0,0,1,0,0,1,1,1]),e.STATIC_DRAW),e.enableVertexAttribArray(0),e.vertexAttribPointer(0,2,e.FLOAT,!1,0,0),this.buf=e.createBuffer(),e.bindBuffer(e.ARRAY_BUFFER,this.buf);let t=We*4;e.enableVertexAttribArray(1),e.vertexAttribPointer(1,4,e.FLOAT,!1,t,0),e.vertexAttribDivisor(1,1),e.enableVertexAttribArray(2),e.vertexAttribPointer(2,4,e.FLOAT,!1,t,16),e.vertexAttribDivisor(2,1),e.enableVertexAttribArray(3),e.vertexAttribPointer(3,4,e.FLOAT,!1,t,32),e.vertexAttribDivisor(3,1),e.bindVertexArray(null)}attach(e,t,r){let{gl:i}=this;this.prog=e,i.useProgram(e),this.uView=i.getUniformLocation(e,"uView"),this.uHalo=i.getUniformLocation(e,"uHalo"),i.uniform1i(i.getUniformLocation(e,"uGlow"),t),i.uniform1i(i.getUniformLocation(e,"uLit"),r?1:0)}draw(e,t,r,i){let{gl:n}=this;!this.prog||!t||(n.useProgram(this.prog),n.bindVertexArray(this.vao),n.bindBuffer(n.ARRAY_BUFFER,this.buf),n.bufferData(n.ARRAY_BUFFER,e.subarray(0,t*We),n.DYNAMIC_DRAW),n.uniform2f(this.uView,r,i),n.uniform1f(this.uHalo,Math.max(.05,Te.halo)),n.enable(n.BLEND),n.blendFunc(n.ONE,n.ONE_MINUS_SRC_ALPHA),n.drawArraysInstanced(n.TRIANGLE_STRIP,0,4,t),n.disable(n.BLEND),n.bindVertexArray(null))}};var ni=o=>{if(!o.getExtension("EXT_color_buffer_float"))return!1;let e=o.createTexture(),t=o.createFramebuffer();o.bindTexture(o.TEXTURE_2D,e),o.texImage2D(o.TEXTURE_2D,0,o.RGBA16F,1,1,0,o.RGBA,o.HALF_FLOAT,null),o.bindFramebuffer(o.FRAMEBUFFER,t),o.framebufferTexture2D(o.FRAMEBUFFER,o.COLOR_ATTACHMENT0,o.TEXTURE_2D,e,0);let r=o.checkFramebufferStatus(o.FRAMEBUFFER)===o.FRAMEBUFFER_COMPLETE;return o.bindFramebuffer(o.FRAMEBUFFER,null),o.bindTexture(o.TEXTURE_2D,null),o.deleteFramebuffer(t),o.deleteTexture(e),r||console.warn("[Nebbia] fog prepass framebuffer incomplete, computing the fog in the main pass"),r},kt=class{constructor(e,t,r="night",i="rain"){this.gl=e;this.done=!1;this.parallel=e.getExtension("KHR_parallel_shader_compile");let n=Ae(e,e.VERTEX_SHADER,On),a=l=>Ae(e,e.FRAGMENT_SHADER,l),s=Xn(r,i);this.programs={fog:t?be(e,n,a(s.fog),{aPos:0}):null,main:be(e,n,a(t?s.main:s.mainFallback),{aPos:0}),sprite:be(e,Ae(e,e.VERTEX_SHADER,Kn),a(jn),{aCorner:0,aRect:1,aParam:2}),mask:be(e,Ae(e,e.VERTEX_SHADER,In),a(Nn),{aCorner:0,aSeg:1,aParam:2}),bldFill:r==="night"?be(e,n,a(Hn),{aPos:0}):null,noise:be(e,n,a(Vn),{aPos:0}),flake:i==="snow"?be(e,Ae(e,e.VERTEX_SHADER,Zn),a(ei),{aCorner:0,aA:1,aB:2,aC:3}):null,firefly:i==="dew"?be(e,Ae(e,e.VERTEX_SHADER,ti),a(ri),{aCorner:0,aA:1,aB:2,aC:3}):null}}poll(){if(this.done)return!0;let{gl:e,parallel:t}=this,r=[this.programs.fog,this.programs.main,this.programs.sprite,this.programs.mask,this.programs.bldFill,this.programs.noise,this.programs.flake,this.programs.firefly].filter(i=>!!i);if(t&&!r.every(i=>e.getProgramParameter(i,t.COMPLETION_STATUS_KHR)))return!1;for(let i of r)Ln(e,i);for(let i of r)for(let n of e.getAttachedShaders(i)??[])e.deleteShader(n);return this.done=!0,!0}};var ii=["uSky0","uSky1","uLa","uLb","uLc"],oi=["uRes","uSceneShift","uTime","uBus","uNeon",...ii,"uSpotSrc","uSpotSrc2","uSpotTgt","uSpotR","uSpotI","uScenery","uBldOn","uBldGeo","uJelly","uJellyC","uJellyN","uBubbles","uSway","uBurst","uBurstN","uBeatRows","uWeedIdle"],So=[...oi,"uDewGlint","uMist","uEdge","uRefract","uGlint","uGlassShift","uBreathOn","uBreathThick","uNoiseOn","uSeeThrough","uMistTint","uWallpaper"],Mo=1,To={on:[0,0,0],geo:[0,1,1,0,1,1,0,1,1]},wr=(o,e,t)=>Object.fromEntries(t.map(r=>[r,o.getUniformLocation(e,r)])),Bt=class{constructor(e,t=!1,r="night",i="rain"){this.canvas=e;this.seeThrough=t;this.U={};this.F={};this.B={};this.uView=null;this.uFogB=null;this.linked=!1;this.W=1;this.H=1;this.spriteReady=!1;this.breathVersion=-1;this.breathW=0;this.breathH=0;this.fieldValid=!1;this.instData=new Float32Array(0);this.backdrop=parent.document.querySelector("canvas[data-music-background]");this.swatch=document.createElement("canvas");this.paperTexture=null;this.paperVersion="";this.paperTime=-1/0;this.palette=null;this.mistTint=[.07,.08,.1];this.onSpriteReady=null;let n=e.getContext("webgl2",{antialias:!1,alpha:t,preserveDrawingBuffer:!0});if(!n)throw new Error("Nebbia: WebGL2 unavailable");this.gl=n,this.fogOK=ni(n),this.build=new kt(n,this.fogOK,r,i),this.quadVao=n.createVertexArray(),n.bindVertexArray(this.quadVao),n.bindBuffer(n.ARRAY_BUFFER,n.createBuffer()),n.bufferData(n.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),n.STATIC_DRAW),n.enableVertexAttribArray(0),n.vertexAttribPointer(0,2,n.FLOAT,!1,0,0),this.mask=new wt(n,2),this.drop=J(n,3),this.field=J(n,4),this.sprite=J(n,5),this.fogGl=new yt(n,6,this.fogOK,i==="dew"&&!t?1:null),this.breath=J(n,8),this.bld=r==="night"?new xt(n,9):null,this.noise=new gt(n,15),this.snow=i==="snow"?new Tt(n,1):null,this.fireflies=i==="dew"?new Lt(n):null,this.spriteVao=n.createVertexArray(),n.bindVertexArray(this.spriteVao),n.bindBuffer(n.ARRAY_BUFFER,n.createBuffer()),n.bufferData(n.ARRAY_BUFFER,new Float32Array([0,0,1,0,0,1,1,1]),n.STATIC_DRAW),n.enableVertexAttribArray(0),n.vertexAttribPointer(0,2,n.FLOAT,!1,0,0),this.instBuf=n.createBuffer(),n.bindBuffer(n.ARRAY_BUFFER,this.instBuf),n.enableVertexAttribArray(1),n.vertexAttribPointer(1,4,n.FLOAT,!1,24,0),n.vertexAttribDivisor(1,1),n.enableVertexAttribArray(2),n.vertexAttribPointer(2,2,n.FLOAT,!1,24,16),n.vertexAttribDivisor(2,1),n.bindVertexArray(null),this.loadSprite()}get isContextLost(){return this.gl.isContextLost()}get ready(){if(this.linked)return!0;if(!this.build.poll())return!1;let{gl:e}=this,{main:t,fog:r,sprite:i,mask:n,bldFill:a,flake:s,firefly:l}=this.build.programs;this.mask.attach(n),this.U=wr(e,t,So),e.useProgram(t);let[c,u,h]=this.fogGl.layers,m=[["uMask",this.mask.tex],["uDrop",this.drop],["uField",this.field],["uFogBlur",c.tex],["uFogGlow",u.tex],["uBreath",this.breath],["uNoise",this.noise.tex]];h&&m.push(["uFogSharp",h.tex]);for(let[f,b]of m)e.uniform1i(e.getUniformLocation(t,f),b.unit);return e.uniform1f(this.U.uBreathThick,_.breathThick),e.uniform1i(this.U.uSeeThrough,this.seeThrough?1:0),this.bld?.bindSamplers(t),r&&(this.F=wr(e,r,oi),this.uFogB=e.getUniformLocation(r,"uFogB"),this.bld?.bindSamplers(r)),a&&(this.B=wr(e,a,[...ii,...Bn])),s&&this.snow?.attach(s,u.tex.unit,c.tex.unit,this.fogOK&&!this.seeThrough),l&&this.fireflies?.attach(l,u.tex.unit,this.fogOK&&!this.seeThrough),e.useProgram(i),this.uView=e.getUniformLocation(i,"uView"),e.uniform1i(e.getUniformLocation(i,"uSprite"),this.sprite.unit),this.linked=!0,this.palette&&this.applyPalette(this.palette),!0}loadSprite(){let e=new Image;e.onload=()=>{let{gl:t}=this;t.isContextLost()||(t.activeTexture(t.TEXTURE0+this.sprite.unit),t.bindTexture(t.TEXTURE_2D,this.sprite.t),t.texImage2D(t.TEXTURE_2D,0,t.RGBA,t.RGBA,t.UNSIGNED_BYTE,e),t.generateMipmap(t.TEXTURE_2D),t.texParameteri(t.TEXTURE_2D,t.TEXTURE_MIN_FILTER,t.LINEAR_MIPMAP_LINEAR),this.spriteReady=!0,this.onSpriteReady?.())},e.src=Fn}setPalette(e,t,r,i,n){this.palette=[e,t,r,i,n],this.linked&&this.applyPalette(this.palette)}applyPalette([e,t,r,i,n]){let{gl:a}=this,{main:s,fog:l,bldFill:c}=this.build.programs;for(let[u,h]of[[s,this.U],[l,this.F],[c,this.B]])u&&(a.useProgram(u),a.uniform3fv(h.uSky0,e),a.uniform3fv(h.uSky1,t),a.uniform3fv(h.uLa,r),a.uniform3fv(h.uLb,i),a.uniform3fv(h.uLc,n));this.bld?.invalidate()}resize(e,t,r){let{gl:i}=this;this.W=Math.max(1,Math.round(e)),this.H=Math.max(1,Math.round(t)),this.canvas.width=Math.max(1,Math.round(e*r)),this.canvas.height=Math.max(1,Math.round(t*r)),this.mask.resize(e,t),this.fieldValid=!1;for(let n of[this.drop,this.field])i.activeTexture(i.TEXTURE0+n.unit),i.bindTexture(i.TEXTURE_2D,n.t),i.texImage2D(i.TEXTURE_2D,0,i.RGBA,this.W,this.H,0,i.RGBA,i.UNSIGNED_BYTE,null),n.fb||(n.fb=i.createFramebuffer()),i.bindFramebuffer(i.FRAMEBUFFER,n.fb),i.framebufferTexture2D(i.FRAMEBUFFER,i.COLOR_ATTACHMENT0,i.TEXTURE_2D,n.t,0);i.bindFramebuffer(i.FRAMEBUFFER,null),this.fogGl.resize(this.canvas.width,this.canvas.height),this.noise.resize(this.canvas.width,this.canvas.height),this.bld?.resize(this.canvas.width,this.canvas.height,Math.max(1,Math.ceil(this.canvas.height/Je)))}renderDrops(e,t){let{gl:r}=this;if(r.bindFramebuffer(r.FRAMEBUFFER,e.fb),r.viewport(0,0,this.W,this.H),r.clearColor(0,0,0,0),r.clear(r.COLOR_BUFFER_BIT),t.length&&this.spriteReady){let i;t instanceof Float32Array?i=t:(this.instData.length<t.length&&(this.instData=new Float32Array(Math.ceil(t.length*1.5))),this.instData.set(t),i=this.instData.subarray(0,t.length)),r.useProgram(this.build.programs.sprite),r.bindVertexArray(this.spriteVao),r.bindBuffer(r.ARRAY_BUFFER,this.instBuf),r.bufferData(r.ARRAY_BUFFER,i,r.DYNAMIC_DRAW),r.uniform2f(this.uView,this.W,this.H),r.enable(r.BLEND),r.blendFuncSeparate(r.ONE_MINUS_DST_COLOR,r.ONE_MINUS_SRC_COLOR,r.ONE,r.ONE_MINUS_SRC_ALPHA),r.drawArraysInstanced(r.TRIANGLE_STRIP,0,4,t.length/6),r.disable(r.BLEND),r.bindVertexArray(null)}r.bindFramebuffer(r.FRAMEBUFFER,null)}setScene(e,t){let{gl:r}=this;r.uniform2f(e.uRes,this.canvas.width,this.canvas.height),r.uniform1f(e.uTime,t.time),r.uniform1f(e.uBus,t.bus),r.uniform1f(e.uNeon,t.neon),r.uniform2fv(e.uSceneShift,t.sceneShift),r.uniform2fv(e.uSpotSrc,t.spotSrc),r.uniform2fv(e.uSpotSrc2,t.spotSrc2),r.uniform2fv(e.uSpotTgt,t.spotTgt),r.uniform1f(e.uSpotR,t.spotR),r.uniform1f(e.uSpotI,t.spotI),r.uniform1i(e.uScenery,t.scenery),r.uniform1f(e.uDewGlint,t.dewGlint),t.aqua&&(r.uniform4fv(e.uJelly,t.aqua.jelly),r.uniform4fv(e.uJellyC,t.aqua.jellyColor),r.uniform1i(e.uJellyN,t.aqua.count),r.uniform1f(e.uBubbles,t.aqua.bubbles),r.uniform4fv(e.uSway,t.aqua.sway),r.uniform4fv(e.uBurst,t.aqua.bursts),r.uniform1i(e.uBurstN,t.aqua.burstCount),r.uniform1f(e.uBeatRows,t.aqua.beatRows),r.uniform1f(e.uWeedIdle,t.aqua.weedIdle))}setBuildings(e,t){let{gl:r}=this;r.uniform3i(e.uBldOn,t.on[0],t.on[1],t.on[2]),r.uniform3fv(e.uBldGeo,t.geo)}render(e,t,r,i,n,a=!0){let{gl:s,U:l}=this,{main:c,fog:u,bldFill:h,noise:m}=this.build.programs;if(this.mask.draw(e),t&&t.version!==this.breathVersion){let{canvas:p}=t;s.activeTexture(s.TEXTURE0+this.breath.unit),s.bindTexture(s.TEXTURE_2D,this.breath.t),p.width===this.breathW&&p.height===this.breathH?s.texSubImage2D(s.TEXTURE_2D,0,0,0,s.RGBA,s.UNSIGNED_BYTE,p):s.texImage2D(s.TEXTURE_2D,0,s.RGBA,s.RGBA,s.UNSIGNED_BYTE,p),this.breathVersion=t.version,this.breathW=p.width,this.breathH=p.height}this.renderDrops(this.drop,r),(a||!this.fieldValid)&&(this.renderDrops(this.field,i),this.fieldValid=this.spriteReady),s.bindVertexArray(this.quadVao);let f=n.scenery&Mo&&!this.seeThrough&&h&&this.bld?this.bld.update(n.time*n.bus,h,this.B):To,b=this.noise.update(m);if(u&&!this.seeThrough&&(s.useProgram(u),this.setScene(this.F,n),this.setBuildings(this.F,f),this.fogGl.draw(this.uFogB)),s.useProgram(c),this.setScene(l,n),this.setBuildings(l,f),s.uniform1f(l.uMist,n.mist),s.uniform1f(l.uEdge,n.edge),s.uniform1f(l.uRefract,n.refract),s.uniform1f(l.uGlint,n.glint),s.uniform2fv(l.uGlassShift,n.glassShift),s.uniform1i(l.uBreathOn,t?1:0),s.uniform1i(l.uNoiseOn,b?1:0),s.uniform3fv(l.uMistTint,this.mistTint),this.seeThrough){let p=this.backdrop,y=performance.now();s.activeTexture(s.TEXTURE0+14),this.paperTexture?s.bindTexture(s.TEXTURE_2D,this.paperTexture):(this.paperTexture=s.createTexture(),s.bindTexture(s.TEXTURE_2D,this.paperTexture),s.texParameteri(s.TEXTURE_2D,s.TEXTURE_MIN_FILTER,s.LINEAR),s.texParameteri(s.TEXTURE_2D,s.TEXTURE_WRAP_S,s.CLAMP_TO_EDGE),s.texParameteri(s.TEXTURE_2D,s.TEXTURE_WRAP_T,s.CLAMP_TO_EDGE),s.texImage2D(s.TEXTURE_2D,0,s.RGBA,1,1,0,s.RGBA,s.UNSIGNED_BYTE,new Uint8Array([110,110,110,255])),this.swatch.width=128,this.swatch.height=72);let g=p?.dataset.glassFrameVersion;if(p?.width&&p.height&&g!==this.paperVersion&&y-this.paperTime>=100){this.paperVersion=g,this.paperTime=y;let d=this.swatch.getContext("2d"),w=p.getBoundingClientRect(),x=window.frameElement?.getBoundingClientRect();x&&w.width&&w.height?d.drawImage(p,(x.left-w.left)*p.width/w.width,(x.top-w.top)*p.height/w.height,x.width*p.width/w.width,x.height*p.height/w.height,0,0,128,72):d.drawImage(p,0,0,128,72),s.texImage2D(s.TEXTURE_2D,0,s.RGBA,s.RGBA,s.UNSIGNED_BYTE,this.swatch)}s.uniform1i(l.uWallpaper,14)}s.viewport(0,0,this.canvas.width,this.canvas.height),s.drawArrays(s.TRIANGLE_STRIP,0,4),s.bindVertexArray(null)}setFlakeAtlas(e,t){this.snow?.setAtlas(e,t)}drawFlakes(e,t,r){this.snow?.draw(e,t,this.W,this.H,[-r[0]*this.W,-r[1]*this.H])}drawFireflies(e,t){this.fireflies?.draw(e,t,this.W,this.H)}destroy(){this.gl.getExtension("WEBGL_lose_context")?.loseContext()}};var tt=null,he=null,Ro=o=>{let e=(r,i)=>r<0||i<0||r>=96||i>=96?0:o[i*96+r],t=!0;for(;t;){t=!1;for(let r=0;r<2;r+=1){let i=[];for(let n=0;n<96;n+=1)for(let a=0;a<96;a+=1){if(!o[n*96+a])continue;let s=[e(a,n-1),e(a+1,n-1),e(a+1,n),e(a+1,n+1),e(a,n+1),e(a-1,n+1),e(a-1,n),e(a-1,n-1)],l=s.reduce((u,h)=>u+h,0);if(l<2||l>6)continue;let c=0;for(let u=0;u<8;u+=1)!s[u]&&s[(u+1)%8]&&(c+=1);c===1&&((r===0?s[0]*s[2]*s[4]||s[2]*s[4]*s[6]:s[0]*s[2]*s[6]||s[0]*s[4]*s[6])||i.push(n*96+a))}for(let n of i)o[n]=0;i.length&&(t=!0)}}},Fo=o=>{let e=[[1,0],[0,1],[-1,0],[0,-1],[1,1],[-1,1],[-1,-1],[1,-1]],t=(l,c)=>l>=0&&c>=0&&l<96&&c<96&&o[c*96+l]===1,r=(l,c)=>e.reduce((u,[h,m])=>u+(t(l+h,c+m)?1:0),0),i=new Uint8Array(9216),n=[],a=(l,c)=>{let u=l,h=c,m=[[u,h]];i[h*96+u]=1;let f=null;for(;;){let b=null,p=-9;for(let[y,g]of e){let d=u+y,w=h+g;if(!t(d,w)||i[w*96+d])continue;let x=f?(y*f[0]+g*f[1])/Math.hypot(y,g):0;x>p&&(p=x,b=[d,w,y,g])}if(!b)break;[u,h]=b,f=[b[2],b[3]],i[h*96+u]=1,m.push([u,h])}m.length>=4?n.push(m):s(m)&&n.push(m.length>1?m:[m[0],[m[0][0]+1,m[0][1]+1]])},s=l=>l.every(([c,u])=>e.every(([h,m])=>!t(c+h,u+m)||l.some(([f,b])=>f===c+h&&b===u+m)));for(let l=0;l<96;l+=1)for(let c=0;c<96;c+=1)o[l*96+c]&&r(c,l)===1&&!i[l*96+c]&&a(c,l);for(let l=0;l<96;l+=1)for(let c=0;c<96;c+=1)o[l*96+c]&&!i[l*96+c]&&a(c,l);return n},Er=(o,e)=>{if(o.length<3)return o;let t=o[0],r=o[o.length-1],i=Math.hypot(r[0]-t[0],r[1]-t[1])||1,n=0,a=0;for(let s=1;s<o.length-1;s+=1){let l=Math.abs((r[0]-t[0])*(t[1]-o[s][1])-(t[0]-o[s][0])*(r[1]-t[1]))/i;l>a&&(a=l,n=s)}return a>e?[...Er(o.slice(0,n+1),e).slice(0,-1),...Er(o.slice(n),e)]:[t,r]},Lo=(o,e)=>{let t=o;for(let r=0;r<e&&t.length>2;r+=1){let i=[t[0]];for(let n=0;n<t.length-1;n+=1){let a=t[n],s=t[n+1];i.push([a[0]*.75+s[0]*.25,a[1]*.75+s[1]*.25],[a[0]*.25+s[0]*.75,a[1]*.25+s[1]*.75])}i.push(t[t.length-1]),t=i}return t},ai=(o,e,t)=>{if(typeof document>"u"||(tt||(tt=document.createElement("canvas"),tt.width=tt.height=96,he=tt.getContext("2d",{willReadFrequently:!0})),!he))return null;he.fillStyle="#000",he.fillRect(0,0,96,96),he.fillStyle="#fff",he.textAlign="center",he.textBaseline="alphabetic",he.font=`${t} ${96*.78}px ${e}`,he.fillText(o,96/2,96*.8);let r=he.getImageData(0,0,96,96).data,i=new Uint8Array(9216);for(let s=0;s<9216;s+=1)i[s]=r[s*4]>110?1:0;Ro(i);let n=Fo(i).map(s=>Lo(Er(s,1.2),2).map(([l,c])=>[l/96,c/96]));for(let s of n)s[0][1]+s[0][0]*.6>s[s.length-1][1]+s[s.length-1][0]*.6&&s.reverse();let a=(s,l)=>s.reduce((c,u)=>Math.min(c,u[l]),1/0);return n.sort((s,l)=>(a(s,1)-a(l,1))*.6+(a(s,0)-a(l,0))),n.length?n:null};var si=async()=>null,_t=()=>null;var li=o=>{let e=0,t=o.filter(r=>r.length>1).map(r=>{let i=[0];for(let n=1;n<r.length;n+=1)i.push(i[n-1]+Math.hypot(r[n][0]-r[n-1][0],r[n][1]-r[n-1][1]));return e+=i[i.length-1],{pts:r,cum:i,len:i[i.length-1]}}).filter(r=>r.len>.005);return t.length?{strokes:t,total:e}:null},ko=(o,e)=>{if(o.length<3)return o;let t=[];for(let r=0;r<o.length-1;r+=1){let i=o[Math.max(0,r-1)],n=o[r],a=o[r+1],s=o[Math.min(o.length-1,r+2)];for(let l=0;l<e;l+=1){let c=l/e,u=c*c,h=u*c,m=f=>.5*(2*n[f]+(-i[f]+a[f])*c+(2*i[f]-5*n[f]+4*a[f]-s[f])*u+(-i[f]+3*n[f]-3*a[f]+s[f])*h);t.push([m(0),m(1)])}}return t.push(o[o.length-1]),t},qe=null,Bo=o=>{let e=bn(o);if(!e||typeof document>"u")return null;qe||(qe=document.createElementNS("http://www.w3.org/2000/svg","svg"),qe.setAttribute("viewBox","0 0 24 24"),qe.style.cssText="position:absolute;width:24px;height:24px;left:-99px;top:-99px;visibility:hidden;pointer-events:none",document.body.appendChild(qe));let t=[];for(let[r,i]of e){let n=document.createElementNS("http://www.w3.org/2000/svg",r);for(let[u,h]of Object.entries(i))u!=="key"&&n.setAttribute(u,String(h));qe.appendChild(n);let a=typeof n.getTotalLength=="function"?n.getTotalLength():0,s=.35,l=[],c=null;for(let u=0;a>0&&u<=a+1e-6;u+=s){let h=n.getPointAtLength(Math.min(u,a)),m=[h.x/24,h.y/24];c&&Math.hypot(m[0]-c[0],m[1]-c[1])>s*3/24&&(l.length>1&&t.push(l),l=[]),l.push(m),c=m}l.length>1&&t.push(l),n.remove()}return t.length?t:null},It=class{constructor(e,t){this.fontFamily=e;this.fontWeight=t;this.cache=new Map;this.font="";this.font=`${t} ${e}`}setFont(e,t){let r=`${t} ${e}`;return r===this.font?!1:(this.fontFamily=e,this.fontWeight=t,this.font=r,this.cache.clear(),!0)}clear(){this.cache.clear()}fontReady(){try{return document.fonts?.check(`${this.fontWeight} 16px ${this.fontFamily}`)??!0}catch{return!0}}get(e){let t=this.cache.get(e);if(t!==void 0)return t;let r=null,i=_t(e);e.startsWith(Ce)?r=Bo(e.slice(Ce.length)):i?r=i.map(a=>ko(a.map(([s,l])=>[s/1024,l/1024]),6)):e.trim()&&(r=ai(e,this.fontFamily,this.fontWeight));let n=r?li(r):null;return this.cache.set(e,n),n}},ci=(o,e,t)=>{if(t<=0)return o;let r=()=>e()*2-1,i=r()*.16*t,n=1+r()*.1*t,a=1+r()*.1*t,s=r()*.035*t,l=r()*.035*t,c=e()*6.28,u=e()*6.28,h=([b,p])=>{let y=(b-.5)*n,g=(p-.5)*a;return y+=g*i,y+=s*Math.sin(g*Math.PI*1.6+c),g+=l*Math.sin(y*Math.PI*1.6+u),[y+.5,g+.5]},m=S(8/o.strokes.length,.35,1),f=o.strokes.map(b=>{let p=b.pts,y=p.length,g=p.reduce((D,U)=>D+U[0],0)/y,d=p.reduce((D,U)=>D+U[1],0)/y,w=r()*.09*t*m,x=1+r()*.1*t*m,E=r()*.022*t*m,v=r()*.022*t*m,A=Math.cos(w),M=Math.sin(w),[T,F]=p[0],R=.006*t,L=18+e()*14,P=e()*6.28;return p.map(([D,U],j)=>{let z=T+(D-T)*x,Z=F+(U-F)*x,se=z-g,V=Z-d;z=g+se*A-V*M+E,Z=d+se*M+V*A+v;let ie=p[Math.max(0,j-1)],ne=p[Math.min(y-1,j+1)],k=ne[0]-ie[0],C=ne[1]-ie[1],O=Math.hypot(k,C)||1,B=Math.sin(b.cum[j]*L+P)*R;return h([z-C/O*B,Z+k/O*B])})});return li(f)??o},we=(o,e)=>{let{pts:t,cum:r}=o,i=1;for(;i<t.length-1&&r[i]<e;)i+=1;let n=t[i-1],a=t[i],s=Math.max(1e-6,r[i]-r[i-1]),l=S((e-r[i-1])/s),c=(a[0]-n[0])/s,u=(a[1]-n[1])/s;return[n[0]+(a[0]-n[0])*l,n[1]+(a[1]-n[1])*l,-u,c]};var _o=/[぀-ヿ㐀-鿿豈-﫿가-힯]/,Nt=/^[，。、！？!?,.・…]$/,Io=/^[ー—～〜…-]$/,No=new Set([..."\u7684\u4E86\u7740\u8FC7\u5417\u5462\u5427\u554A\u5440\u561B\u4E4B\u4E0E\u548C\u662F\u4E5F\u90FD\u5C31\u8FD8\u53C8\u88AB\u628A\u7ED9\u306E\u304C\u3092\u306B\u306F\u3078\u3068\u3067\u3084\u3082\u306D\u3088\u306A\u304B\u3057\u3066\u305F\u3063"]),Ar={hero:2,semi:1.35,support:1,particle:.78},Sr={hero:1.55,semi:1.18,support:1,particle:.85},te=.3,re=.32,Mr=o=>/[　-鿿＀-￯]/.test(o),Po=o=>Ge(o)?1.1:o===" "?.32:/[，。、・．]/.test(o)?.5:Mr(o)?1:/[il.,'!|:;]/.test(o)?.3:/[mwMW]/.test(o)?.72:.5,ui=(o,e)=>{let t=[],r=i=>i.every(n=>/^[゠-ヿ]$/.test(o[n].ch));for(let i of e){let n=t[t.length-1];n&&(i.every(a=>Nt.test(o[a].ch))||r(i)&&r(n)&&i[0]===n[n.length-1]+1)?n.push(...i):t.push([...i])}return t},Do=(o,e)=>{let t=e.filter(n=>o[n].ch.trim()).length,r=o[e[0]].t,i=o[e[e.length-1]];return Math.min(t,8)*14+S(i.t+i.d-r,0,2.5)*18},hi=(o,e)=>{let t=e.map(n=>Do(o,n)),r=0;t.forEach((n,a)=>{n>t[r]&&(r=a)});let i=-1;return e.length>=4&&e.forEach((n,a)=>{a===r||Math.abs(a-r)<2||n.length<2||t[a]<t[r]*.35||(i<0||t[a]>t[i])&&(i=a)}),e.map((n,a)=>a===r&&e.length>1?"hero":a===i?"semi":n.length===1&&No.has(o[n[0]].ch)?"particle":"support")},Co=(o,e)=>e.every(t=>_o.test(o[t].ch)||Nt.test(o[t].ch)),Pt=(o,e,t,r,i)=>{let n=[];e.forEach((l,c)=>{let u=t[c],h={idx:l,scale:r[u],cjk:Co(o,l)},m=n[n.length-1];u==="particle"&&m&&m.role!=="hero"&&m.role!=="semi"?m.words.push(h):n.push({role:u,words:[h]})});let a=l=>l.words.reduce((c,u)=>c+u.idx.length,0),s=l=>l.role==="hero"||l.role==="semi";for(;n.length>i;){let l=-1,c=1/0;for(let u=0;u<n.length-1;u+=1)s(n[u])||s(n[u+1])||a(n[u])+a(n[u+1])<c&&(c=a(n[u])+a(n[u+1]),l=u);if(l<0)break;n[l]={role:"support",words:[...n[l].words,...n[l+1].words]},n.splice(l+1,1)}return n},Go=(o,e)=>o.cjk&&e.cjk?.04:.24,Uo=(o,e)=>{let t=_t(o)?.length??0;return 1+.25*S((t-9)/10)/Math.max(1,e)},mi=(o,e,t)=>{let r=[],i=0,n=0;return o.words.forEach((a,s)=>{s>0&&(i+=Go(o.words[s-1],a)*(t==="v"&&a.cjk?0:1));for(let l of a.idx){let c=e[l].ch,u=t==="v"&&a.cjk,h=u?Nt.test(c)?.55:1.02:Po(c),m=t==="v"&&(!a.cjk||Io.test(c))?Math.PI/2:0,f=a.scale*(a.cjk?Uo(c,a.scale):1);r.push({k:l,off:i+h*f/2,s:f,rot:m,punct:u&&Nt.test(c)}),i+=h*f,n=Math.max(n,f)}}),t==="h"?{dir:t,items:r,w:i,h:n}:{dir:t,items:r,w:n,h:i}},$=(o,e,t,r)=>{for(let i of o.items)o.dir==="h"?r.push({k:i.k,x:e-o.w/2+i.off,y:t+o.h/2-i.s/2,s:i.s,rot:0}):r.push({k:i.k,x:e+(i.punct?i.s*.3:0),y:t-o.h/2+i.off-(i.punct?i.s*.2:0),s:i.s,rot:i.rot})},Re=o=>o.items.some(e=>e.rot)?1:0;var X=(o,e,t)=>mi(o.blocks[e],o.chars,t),Tr=(o,e,t)=>{let r=[],i=e.map(l=>l.map(c=>X(o,c,"h"))),n=i.map(l=>l.reduce((c,u)=>c+u.w,0)+te*(l.length-1)),a=Math.max(...n),s=0;return i.forEach((l,c)=>{let u=Math.max(...l.map(m=>m.h)),h=-n[c]/2+t[c]*a;l.forEach(m=>{$(m,h+m.w/2,s+u-m.h/2,r),h+=m.w+te}),s+=u+re}),{items:r,vRot:0}},pe={ribbon:o=>Tr(o,[o.blocks.map((e,t)=>t)],[0]),lines2(o){let e=o.blocks.length;if(e<2)return null;let t=o.blocks.map((s,l)=>X(o,l,"h")),r=t.reduce((s,l)=>s+l.w,0),i=0,n=1,a=1/0;return t.forEach((s,l)=>{i+=s.w,l<e-1&&Math.abs(i-r/2)<a&&(a=Math.abs(i-r/2),n=l+1)}),Tr(o,[[...Array(n).keys()],[...Array(e-n).keys()].map(s=>s+n)],[-.12,.12])},lines3(o){let e=o.blocks.length;if(e<3)return null;let t=o.blocks.map((a,s)=>X(o,s,"h")),r=t.reduce((a,s)=>a+s.w,0),i=[[]],n=0;return t.forEach((a,s)=>{i[i.length-1].length&&i.length<3&&(n+a.w/2>r/3*i.length||e-s<=3-i.length)&&i.push([]),i[i.length-1].push(s),n+=a.w}),i.length===3?Tr(o,i,[-.14,.1,-.04]):null},cross(o){let{blocks:e,h:t}=o,r=e.length;if(r<3||r>7)return null;let i=Math.floor(t/2),n=Math.ceil((r-1-t)/2);if(i===0&&t+n+1>=r)return null;let a=[],s=X(o,t,"h");$(s,0,0,a);let l=0,c=-s.w/2-re;for(let h=t-1;h>=i;h-=1){let m=X(o,h,"h");$(m,c-m.w/2,(s.h-m.h)/2,a),c-=m.w+te}let u=-s.h/2-re;for(let h=i-1;h>=0;h-=1){let m=X(o,h,"v");l+=Re(m),$(m,0,u-m.h/2,a),u-=m.h+re}c=s.w/2+re;for(let h=t+1;h<=t+n;h+=1){let m=X(o,h,"h");$(m,c+m.w/2,(s.h-m.h)/2,a),c+=m.w+te}u=s.h/2+re;for(let h=t+n+1;h<r;h+=1){let m=X(o,h,"v");l+=Re(m),$(m,0,u+m.h/2,a),u+=m.h+re}return{items:a,vRot:l}},pillar(o){let{blocks:e,h:t}=o,r=e.length;if(r<2||r>6)return null;let i=[],n=X(o,t,"v"),a=Re(n);$(n,0,0,i);let s=-n.h/2+.15,l=(c,u,h)=>{let m=[[]],f=s;for(let p=c;p!==u;p+=1){let y=X(o,p,"v");a+=Re(y),f+y.h>n.h/2+.6&&m[m.length-1].length&&(m.push([]),f=s),m[m.length-1].push(y),f+=y.h+re}let b=h*(n.w/2+te);for(let p of h>0?m:[...m].reverse()){f=s;for(let y of p)$(y,b+h*y.w/2,f+y.h/2,i),f+=y.h+re;b+=h*(Math.max(0,...p.map(y=>y.w))+te)}};return l(0,t,-1),l(t+1,r,1),{items:i,vRot:a}},header(o){let{blocks:e,h:t}=o,r=e.length;if(r<3||r-1-t<2)return null;let i=[],n=X(o,t,"h");if($(n,0,0,i),t>0){let c=e.slice(0,t).map((b,p)=>X(o,p,"h")),u=c.reduce((b,p)=>b+p.w,0)+te*(t-1),h=Math.max(...c.map(b=>b.h)),m=-u/2,f=-n.h/2-re-h/2;c.forEach(b=>{$(b,m+b.w/2,f+(h-b.h)/2,i),m+=b.w+te})}let a=n.h/2+re,s=-n.w*.22-te/2,l=n.w*.22+te/2;for(let c=t+1;c<r;c+=2){let u=X(o,c,"h"),h=c+1<r?X(o,c+1,"h"):null,m=Math.max(u.h,h?h.h:0);$(u,s-u.w/2,a+m-u.h/2,i),h&&$(h,l+h.w/2,a+m-h.h/2,i),a+=m+re}return{items:i,vRot:0}},badge(o){let{blocks:e,h:t}=o,r=e.length;if(r<3||r>7||t!==0)return null;let i=[],n=X(o,0,"v"),a=e.slice(1).map((m,f)=>X(o,f+1,"h")),s=Math.max(Math.max(...a.map(m=>m.w)),n.h*.9);$(n,n.w/2,n.h/2,i);let l=n.w+te,c=0,u=0,h=()=>c<n.h?n.w+te:0;return a.forEach(m=>{l>h()&&l+m.w>n.w+te+s&&(c+=u+re,l=h(),u=0),$(m,l+m.w/2,c+m.h/2,i),l+=m.w+te,u=Math.max(u,m.h)}),{items:i,vRot:Re(n)}},columns(o){let{blocks:e}=o,t=e.length;if(t<2||t>5||e.some(n=>n.words.some(a=>!a.cjk)))return null;let r=[],i=0;return e.forEach((n,a)=>{let s=X(o,a,"v");$(s,i+s.w/2,s.h/2+a%2*.5,r),i+=s.w+re}),{items:r,vRot:0}},orbit(o){let{blocks:e,h:t}=o,r=e.length;if(r<4||r>7)return null;let i=[],n=X(o,t,"h");$(n,0,0,i);let a=[{x0:-n.w/2,x1:n.w/2,y0:-n.h/2,y1:n.h/2}],s=(h,m)=>Math.max(h.x0-m.x1,m.x0-h.x1,h.y0-m.y1,m.y0-h.y1),l=-Math.PI*.75,c=0,u=Math.hypot(n.w,n.h)/2+re;for(let h=0;h<r;h+=1){if(h===t)continue;let m=u,f=l,b=null,p=X(o,h,"h");for(let y=0;y<600&&!b;y+=1){p=X(o,h,Math.abs(Math.cos(f))>.75?"v":"h");let g=Math.cos(f)*m*1.25,d=Math.sin(f)*m*.8,w={x0:g-p.w/2,x1:g+p.w/2,y0:d-p.h/2,y1:d+p.h/2};a.every(x=>s(x,w)>=te)?b=w:(f+=.08,y%80===79&&(m+=.5))}if(!b)return null;a.push(b),c+=Re(p),$(p,(b.x0+b.x1)/2,(b.y0+b.y1)/2,i),l=f+.5}return{items:i,vRot:c}}},fi={ribbon:12,lines2:10,lines3:14,cross:0,pillar:1,header:1,badge:4,columns:2,orbit:4},Rr=new Set(["ribbon","lines2","lines3"]);var Oo=.03,Dt=o=>{let e=1/0,t=-1/0,r=1/0,i=-1/0;for(let n of o)e=Math.min(e,n.x-n.s/2),t=Math.max(t,n.x+n.s/2),r=Math.min(r,n.y-n.s/2),i=Math.max(i,n.y+n.s/2);return{x0:e,x1:t,y0:r,y1:i,w:t-e,h:i-r}},Wo=(o,e)=>Math.max(0,Math.min(o.x1,e.x1)-Math.max(o.x0,e.x0))*Math.max(0,Math.min(o.y1,e.y1)-Math.max(o.y0,e.y0)),bi=(o,e)=>{let{W:t,H:r,jitter:i,layouts:n,fontScale:a,visibleUntil:s,fogAt:l,lingerUntil:c}=e,u=t*.05,h=r*.07,m=Math.max(26,r*.065)*a,f=r*.14*a,b=[],p=[],y=null,g=null,d=(x,E)=>{let v=(E==="title"?null:pe.ribbon(x))??w(x),A=Dt(v.items),M=E==="title"?f*1.15:f*.55,T=Math.min(M,(t-u*2)*.8/A.w,r*.32/A.h),F=A.w*T,R=A.h*T,L=E==="title"||!g?r*.42:g.y1+R/2+T*.6,P={x0:t/2-F/2,x1:t/2+F/2,y0:L-R/2,y1:L+R/2};return E==="title"&&(g=P),{score:0,name:"ribbon",items:v.items,bb:A,S:T,cx:t/2,cy:L,box:P}},w=x=>{let E=pe.ribbon(x),v=Dt(E.items);return(Math.min(f*1.15,(t-u*2)*.8/v.w)>=m?null:pe.lines2(x))??E};return o.map((x,E)=>{let{chars:v}=x,A=W(de(v.map(k=>k.ch).join(""))+E*7919>>>0),M=v.filter(k=>k.ch.trim()),T=M.length>0&&(x.end-x.t)/M.length<.22,F=ui(v,x.words);if(!F.length)return v.map(()=>({x:t/2,y:r/2,s:m,rot:0}));let R=hi(v,F),L=T?Sr:Ar,P=b.filter(k=>k.linger>x.t-.3).map(k=>({box:k.box,w:k.until<=x.t-.3?Oo:x.t<=k.fogAt?1:.6+.4*S((k.until-x.t)/Math.max(.001,k.until-k.fogAt))})),D=x.credits?d({blocks:Pt(v,F,F.map(()=>"support"),Ar,9),h:0,chars:v},x.credits):null;if(!D)for(let k of[5,7]){let C=Pt(v,F,R,L,k),O={blocks:C,h:Math.max(0,C.findIndex(B=>B.role==="hero")),chars:v};for(let B of Object.keys(pe)){if(!n.includes(B))continue;let G=pe[B](O);if(!G||!G.items.length)continue;let Y=Dt(G.items),Pe=r*(B==="ribbon"?.28:.5),De=(t-u*2)*(Rr.has(B)?.94:.68),le=Math.min(De/Y.w,Pe/Y.h,f);if(!(le<m*.75))for(let oe of[le,le*.8,le*.64,le*.5].filter((ee,K)=>K===0||ee>=m*.75)){let ee=Y.w*oe,K=Y.h*oe;for(let cr of[.5,.3,.7])for(let Qi of[.25,.42,.58,.75]){let st=S(cr*t,u+ee/2,t-u-ee/2),Xe=S(Qi*r,h+K/2,r-h-K/2),Ke={x0:st-ee/2,x1:st+ee/2,y0:Xe-K/2,y1:Xe+K/2},lt=oe*.3,Zi={x0:Ke.x0-lt,x1:Ke.x1+lt,y0:Ke.y0-lt,y1:Ke.y1+lt},ae=0;for(let Zr of P)ae+=Wo(Zi,Zr.box)/(ee*K)*1e3*Zr.w;ae-=oe/r*100,oe<m&&(ae+=40+(m-oe)/m*200),ae+=fi[B],ae+=G.vRot*(T?10:4),T&&!Rr.has(B)&&(ae+=6),k===7&&C.length<=5&&(ae+=100);let Qr=p.lastIndexOf(B);Qr>=0&&(ae+=[18,9,4][p.length-1-Qr]),y&&Math.abs(y.cx-st)<t*.08&&Math.abs(y.cy-Xe)<r*.08&&(ae+=8),ae+=Math.abs(Xe-r*.5)/r*8,ae+=A()*8,(!D||ae<D.score)&&(D={score:ae,name:B,items:G.items,bb:Y,S:oe,cx:st,cy:Xe,box:Ke})}}}}if(!D){let C={blocks:Pt(v,F,R,Sr,9),h:0,chars:v},O=pe.lines3(C)??pe.lines2(C)??pe.ribbon(C),B=Dt(O.items),G=Math.min(f,(t-u*2)/B.w,(r-h*2)/B.h);D={score:0,name:"lines3",items:O.items,bb:B,S:G,cx:t/2,cy:r/2,box:{x0:t/2-B.w*G/2,x1:t/2+B.w*G/2,y0:r/2-B.h*G/2,y1:r/2+B.h*G/2}}}let U=D;b.push({box:U.box,until:s(E),fogAt:l(E),linger:Math.max(s(E),c?.(E)??0)}),p.push(U.name),p.length>3&&p.shift(),y=U;let{bb:j,S:z,cx:Z,cy:se}=U,V=(A()-.5)*.04*i,ie=new Map(U.items.map(k=>[k.k,k])),ne={x:Z,y:se,s:z,rot:0};return v.map((k,C)=>{let O=ie.get(C);if(!O)return{...ne,rot:0};let B=Z+(O.x-(j.x0+j.x1)/2)*z,G=se+(O.y-(j.y0+j.y1)/2)*z,Y=O.s*z*(1+(A()-.5)*.08*i);return ne={x:B+(A()-.5)*z*.04*i,y:G+(B-Z)*V+(A()-.5)*z*.05*i,s:Y,rot:O.rot+(A()-.5)*.08*i+V},ne})})};var qo=(o,e,t)=>o.length===1?o[0]:W(t+e*977+3>>>0)()<.5?"fog":"fingers",Ho=(o,e,t)=>Math.min(e,o+6)+t,zo=16,Vo=3,Xo=(o,e,t,r)=>{let i=ci(e,W(Math.floor(t()*1e9)),r);o.glyph=i,o.beads=i.strokes.map(n=>{let a=[],s=7/o.s;for(let l=s*t();l<n.len;l+=s*(.5+t())){if(t()>_.beads)continue;let[c,u,h,m]=we(n,l),f=t()<.5?-1:1;a.push({s:l,x:c,y:u,nx:h*f,ny:m*f,r:1.6+t()**2.2*3.6,j:t()})}return a}),o.drips=i.strokes.map(()=>t()<_.drip*.22?{delay:.3+t()*1.6,len:30+t()*130,dur:2+t()*3.5,r:3.2+t()*2.6,seed:t()*100}:null),o.ends=i.strokes.map(()=>({h:t(),j:t()}))},Ko=(o,e)=>(o.total*.28+(o.strokes.length-1)*.06)/e,Fr=(o,e,t)=>{let r=-1/0;return{starts:o.map((n,a)=>{let s=Math.max(n.t,r);return r=s+e[a]*t,s}),end:r}},jo=(o,e,t,r)=>{let i=e.appear.filter(f=>f!=="fade"),n=e.appear.includes("fade"),a=i.length?i[Math.floor(W(r+t*613+11>>>0)()*i.length)]:null,s=o.chars.filter(f=>f.glyph),l=s.map(f=>Ko(f.glyph,e.writeSpeed)),c=Math.max(o.next,o.end),u=1;if(Fr(s,l,1).end>c){let f=.3,b=1;for(let p=0;p<14;p+=1){let y=(f+b)/2;Fr(s,l,y).end>c?b=y:f=y}u=f}let h=Fr(s,l,u),m=!a||n&&s.length>0&&h.end>c+.001;s.forEach((f,b)=>{f.wt=h.starts[b],f.wd=l[b]*u}),a==="breath"&&!m&&s.length&&(o.appearBreathAt=s[0].wt-_.breathSpread-.3,o.appearBreathUntil=o.end+_.breathHold);for(let f of o.chars)f.appearDur=m?S(f.d*3,.35,1.2):0},Yo=(o,e)=>{let t=e.findIndex(l=>l.credits==="title"),r=e.findIndex(l=>l.credits==="artist");if(t<0||r<0)return;let i=o[t].chars.filter(l=>l.glyph),n=i[i.length-1];if(!n)return;let a=o[r].chars.find(l=>l.glyph),s=a?Math.max(0,n.wt+n.wd+.4-a.wt):0;for(let l of o[r].chars)l.wt+=s},Fe=o=>{let e=1/0,t=-1/0,r=1/0,i=-1/0,n=0;for(let a of o.chars)a.glyph&&(e=Math.min(e,a.x-a.s/2),t=Math.max(t,a.x+a.s/2),r=Math.min(r,a.y-a.s/2),i=Math.max(i,a.y+a.s/2),n=Math.max(n,a.s));return{x0:e,x1:t,y0:r,y1:i,S:n}},pi=(o,e,t,r,i)=>{let n=de(String(i)),a=o.map((d,w)=>d.credits?"fog":qo(t.fade,w,n)),s=d=>t.refog+(a[d]==="fog"?0:2.5),l=o.find(d=>d.credits)?.t??1/0,c=d=>Math.max(o[d].end+.5,l-s(d)*.6),u=d=>o[d].credits?1/0:Math.min(Ho(o[d].end,o[d].next,t.hold),c(d)),h=d=>Math.max(u(d),Math.min(u(d)+zo,(o[d+Vo]?.t??1/0)+t.hold,c(d))),m=d=>u(d)+s(d),f=bi(o,{W:r.W,H:r.H,jitter:t.jitter,layouts:t.layouts,fontScale:r.fontScale,visibleUntil:m,fogAt:u,lingerUntil:d=>h(d)+s(d)}),b=o.map((d,w)=>({t:d.t,end:d.end,next:d.next,mode:a[w],fogAt:u(w),yieldAt:1/0,breathAt:1/0,breathUntil:1/0,breathDur:_.breathSpread,appearBreathAt:1/0,appearBreathUntil:1/0,chars:d.chars.map((x,E)=>{let v=f[w][E],A=Mr(x.ch)||Ge(x.ch),M={...x,x:v.x,y:v.y,s:v.s*(A?1:1.15),rot:v.rot,glyph:null,beads:[],drips:[],ends:[],appearDur:0,wt:x.t,wd:0},T=e.get(x.ch);return T&&Xo(M,T,W(w*131+E*17+5>>>0),t.warp),M})}));b.forEach((d,w)=>jo(d,t,w,n)),Yo(b,o);let p=b.map(Fe),{breathSpread:y,breathHold:g}=_;return b.forEach((d,w)=>{if(o[w].credits)return;let x=h(w)+s(w),E=-1;for(let v=w+1;v<b.length&&b[v].t<x;v+=1){let A=p[w],M=p[v];if(Math.min(A.x1,M.x1)>Math.max(A.x0,M.x0)&&Math.min(A.y1,M.y1)>Math.max(A.y0,M.y0)){d.yieldAt=$o(b[v].t,d),E=v;break}}if(d.fogAt=Math.max(u(w),Math.min(h(w),d.yieldAt-s(w))),E>=0&&!o[E].credits&&d.yieldAt<d.fogAt+s(w)){let v=Math.max(d.yieldAt-y-.3,yi(d)+xi);d.breathAt=v,d.breathDur=Math.max(.3,d.yieldAt-.3-v),d.breathUntil=b[E].end+g}else E>=0&&d.mode==="fingers"&&(b[E].appearBreathAt=1/0)}),b},di=.6,xi=.3,$o=(o,e)=>Math.max(o,yi(e)+xi+di),yi=o=>{let e=o.t;for(let t of o.chars)t.glyph&&(e=Math.max(e,t.wd>0?t.wt+t.wd:t.t+Math.min(t.appearDur,.5)));return e},Lr=(o,e)=>o.breathAt<1/0?1-N(S(((e-o.breathAt)/o.breathDur-.15)/.7)):1-N(S((e-o.yieldAt+di)/1)),Jo=.08,gi=(o,e)=>{let t=Lr(o,e);return o.breathAt<1/0?Math.max(t,Jo):t};var vi=()=>null;var rt=1/50,kr=(o,e)=>{let t=0,r=o.length;for(;t<r;){let i=t+r>>1;o[i]<e?t=i+1:r=i}return t},wi=new WeakMap,Qo=o=>{let e=wi.get(o);if(e)return e;let{db:t,top:r,fps:i}=o,n=t.length,a=Math.round(1.5*i),s=r-28,l=new Float32Array(n),c=[];for(let u=0;u<Math.min(n,a);u+=1)t[u]>s&&c.splice(kr(c,t[u]),0,t[u]);for(let u=0;u<n;u+=1){if(u>0){let f=u+a-1,b=u-a-1;f<n&&t[f]>s&&c.splice(kr(c,t[f]),0,t[f]),b>=0&&t[b]>s&&c.splice(kr(c,t[b]),1)}if(t[u]<=s)continue;let h=c[Math.floor(c.length*.1)],m=Math.max(h+3,c[Math.floor(c.length*.95)]);l[u]=S((t[u]-h)/(m-h))**1.2}return wi.set(o,l),l},Zo=o=>S(2**((o+17.4)/8),.5,3),ea=(o,e)=>o.find(t=>e>=t.t-.3&&e<=t.end+1)??null,Ct=class{constructor(){this.floor=0;this.peak=1;this.value=0}update(e,t){let r=1-Math.exp(-t/3);return this.peak=Math.max(e,this.peak+(e-this.peak)*r*.5),this.floor=Math.min(e,this.floor+(e-this.floor)*r*.3),this.value=S((e-this.floor)/Math.max(this.peak-this.floor,8))**1.2,this.value}},He=class{constructor(e){this.seed=e;this.big=[];this.small=[];this.lastBucket=-1;this.vocal=null;this.vocalFrom=0;this.loud=1}buildBig(e,t,r,i,n=null){this.big=[],this.small=[],this.lastBucket=-1,this.vocal=n&&n.fps===50?Qo(n):null,this.vocalFrom=this.vocal?n?.from??0:0,this.loud=n&&this.vocal?Zo(n.top):1;let a=W(fr(this.seed,911)),s=Math.min(r,i)/720,l=(u,h)=>{let m=h?Fe(h):null,f=0,b=0;for(let g=0;g<8&&(f=r*(.03+.94*a()),b=i*(.03+.94*a()),!(!m||f<m.x0-20||f>m.x1+20||b<m.y0-20||b>m.y1+20));g+=1);let p=(7+a()*6)*s,y=Array.from({length:4+Math.floor(a()*5)},()=>{let g=a()*6.28,d=p*(1.5+a()*1.4);return[Math.cos(g)*d,Math.sin(g)*d*.85+p*.2,(1+a()*1.6)*s]});this.big.push({t:u,x:f,y:b,R:p,big:!0,sat:y,seed:a()*100})},c=n&&this.vocal?this.vocalFrom/n.fps:1/0;if(e.forEach((u,h)=>{for(let m of t[h]??[]){let f=u.chars[m[0]].t;f<c&&a()<.6&&l(f,u)}}),n&&this.vocal){let u=this.vocal;for(let h of n.onsets)(u[h]>.15||a()<.3)&&l(h/n.fps,ea(e,h/n.fps))}this.big.sort((u,h)=>u.t-h.t)}advance(e,t,r,i,n,a){let s=Math.floor(e/rt);if((s<this.lastBucket||s-this.lastBucket>i/rt)&&(this.small=[],this.lastBucket=s-Math.ceil(i/rt)),r>0){let l=Math.min(n,a)/720,c=this.vocal,u=m=>(.15+7*m*m)*r*rt,h=u(t);for(let m=this.lastBucket+1;m<=s;m+=1){let f=W(fr(this.seed+m,23249));f()>=(c&&(m<0||m>=this.vocalFrom)?u(m>=0&&m<c.length?c[m]:0)*this.loud:h)||this.small.push({t:(m+f())*rt,x:n*(.03+.94*f()),y:a*(.03+.94*f()),R:(2+f()*3.5)*l,big:!1,sat:null,seed:f()*100})}}for(this.lastBucket=s;this.small.length&&this.small[0].t<e-i;)this.small.shift()}draw(e,t,r,i,n,a){let s=i*.3,l=.4+s+n*.5+i,c=0,u=this.big.length;for(;c<u;){let m=c+u>>1;this.big[m].t<r-l?c=m+1:u=m}e.setTransform(q,0,0,q,0,0);let h=(m,f)=>{let b=m.R*f,p=r-m.t,y=1-N(S((p-.4)/s)),g=1-N(S((p-.4-n*.5)/i));if(m.big&&y>.002){for(let d=0;d<7;d+=1){let w=m.seed+d*.9,x=b*.3,E=m.x+Math.cos(w)*x,v=m.y+Math.sin(w*1.3)*x;e.moveTo(E,v),e.lineTo(E+.1,v)}e.stroke(b*1.3,y)}if(g>.01){let d=Math.exp(-p/.05);if(t.put(m.x,m.y+b*.1,b*.9*(.6+.4*g),g,1+.5*d,1-.35*d),m.sat)for(let[w,x,E]of m.sat){let v=S(p/.08),A=w*f,M=x*f;t.put(m.x+A*v,m.y+M*v,E*(.55+.45*g),g*v)}}};if(a>0)for(let m=c;m<this.big.length&&this.big[m].t<=r;m+=1)this.big[m].R*a>=1&&h(this.big[m],a);for(let m of this.small)m.t<=r&&h(m,1);e.setTransform(1,0,0,1,0,0)}scheduled(){return{big:this.big,small:this.small}}static lifeOf(e,t){return .4+e*.3+t*.5+e}};var ze=.25,ta=(o,e)=>{let t=W(e*6151+29>>>0),{x0:r,x1:i,y0:n,y1:a,S:s}=Fe(o);if(!Number.isFinite(r))return null;let l=(r+i)/2,c=(n+a)/2,u=(i-r)/2+s*.3,h=(a-n)/2+s*.3,m=[{x:l,y:c,rx:u*.95,ry:h*.95}],f=5+Math.floor(t()*2),b=Math.min(u,h);for(let p=0;p<f;p+=1){let y=(p+t()*.6)/f*Math.PI*2,g=b*(.45+t()*.25);m.push({x:l+Math.cos(y)*u*.6,y:c+Math.sin(y)*h*.6,rx:g,ry:g})}return{lobes:m,cx:l,cy:c,ry:h}},ra=(o,e)=>o.length===e.length&&o.every((t,r)=>t.b===e[r].b&&t.c0===e[r].c0&&t.g0===e[r].g0&&t.b0===e[r].b0&&t.u===e[r].u),Gt=class{constructor(){this.canvas=document.createElement("canvas");this.active=!1;this.version=0;this.puffs=[];this.drawn=[];let e=this.canvas.getContext("2d");if(!e)throw new Error("Nebbia: no 2D context for the breath mask");this.ctx=e}resize(e,t){this.canvas.width=Math.max(1,Math.round(e*ze)),this.canvas.height=Math.max(1,Math.round(t*ze)),this.drawn=[],this.version+=1}begin(){this.puffs=[]}drawLine(e,t,r,i,n,a,s){let{breathFade:l}=_;if(r<i)return;let c=1-N(S((r-Math.max(n,i+a))/l));if(c<=.002)return;e.breath===void 0&&(e.breath=ta(e,t));let u=e.breath;if(!u)return;let h=S((r-i)/a),m=Math.round(255*N(S(h*1.4))),f=s?Math.round(255*N(S(h*2.5))*(1-N(S((h-.8)/.2)))):0;this.puffs.push({b:u,c0:Math.round(255*c),g0:m,b0:f,u:h})}end(){let{puffs:e}=this;if(this.active=e.length>0,ra(e,this.drawn))return;let{ctx:t}=this;this.drawn.length&&(t.setTransform(1,0,0,1,0,0),t.globalCompositeOperation="source-over",t.fillStyle="#000",t.fillRect(0,0,this.canvas.width,this.canvas.height));for(let r of e)this.paint(r);this.drawn=e,this.version+=1}paint({b:e,c0:t,g0:r,b0:i,u:n}){let a=1-(1-n)**3,s=.7+.3*a,l=.08+.92*a,{ctx:c}=this;c.globalCompositeOperation="lighten";for(let u of e.lobes){let h=e.cx+(u.x-e.cx)*s,m=e.cy+(u.y-e.cy)*l;c.setTransform(ze*u.rx*s,0,0,ze*Math.max(u.ry*l,4),ze*h,ze*m);let f=c.createRadialGradient(0,0,0,0,0,1);f.addColorStop(0,`rgb(${t},${r},${i})`),f.addColorStop(.45,`rgb(${Math.round(t*.7)},${r},${i})`),f.addColorStop(.999,`rgb(0,${r},${i})`),f.addColorStop(1,"rgb(0,0,0)"),c.fillStyle=f,c.beginPath(),c.arc(0,0,1,0,Math.PI*2),c.fill()}c.setTransform(1,0,0,1,0,0)}};var Ut=class{constructor(){this.cells=[];this.gw=1;this.gh=1;this.size=30}reset(e,t,r){this.size=r,this.gw=Math.ceil(e/r)+1,this.gh=Math.ceil(t/r)+1,this.cells=Array.from({length:this.gw*this.gh},()=>[])}gx(e){return Math.min(this.gw-1,Math.max(0,Math.floor(e/this.size)))}gy(e){return Math.min(this.gh-1,Math.max(0,Math.floor(e/this.size)))}place(e){if(!this.cells.length)return;let t=this.gx(e.x)+this.gy(e.y)*this.gw;t!==e.cell&&(this.unplace(e),this.cells[t].push(e),e.cell=t)}unplace(e){if(e.cell<0)return;let t=this.cells[e.cell],r=t.indexOf(e);r>=0&&(t[r]=t[t.length-1],t.pop()),e.cell=-1}alongSegments(e,t,r,i,n){for(let a=0;a<e.length;a+=5){let s=e[a+4]/2,l=s+t+r,c=this.gx(Math.min(e[a],e[a+2])-l),u=this.gx(Math.max(e[a],e[a+2])+l),h=this.gy(Math.min(e[a+1],e[a+3])-l),m=this.gy(Math.max(e[a+1],e[a+3])+l);for(let f=h;f<=m;f+=1)for(let b=c;b<=u;b+=1)for(let p of this.cells[b+f*this.gw])!p.dead&&Se(p.x,p.y,e,a)<s+t+i(p)&&n(p)}}at(e,t,r,i){if(!this.cells.length)return null;let n=this.gx(e),a=this.gy(t);for(let s=Math.max(0,a-1);s<=Math.min(this.gh-1,a+1);s+=1)for(let l=Math.max(0,n-1);l<=Math.min(this.gw-1,n+1);l+=1)for(let c of this.cells[l+s*this.gw])if(!c.dead&&Math.hypot(c.x-e,c.y-t)<i(c)+r)return c;return null}};var na=3,ia=.5,Ot=class{constructor(){this.list=[]}clear(){this.list.length=0}track(e,t,r,i){if(!e){let a={pts:[t,r],w:i,end:null};return this.list.push(a),a}let n=e.pts;return e.w=Math.max(e.w*.98,i),Math.hypot(t-n[n.length-2],r-n[n.length-1])>na&&n.push(t,r),e}draw(e,t,r,i){e.setTransform(q,0,0,q,0,0);for(let n=this.list.length-1;n>=0;n-=1){let a=this.list[n],s=a.end===null?1:1-N(S((t-a.end-r*.5)/(i*.6)));if(s<=.002){this.list.splice(n,1);continue}let l=a.pts;if(!(l.length<4)){e.moveTo(l[0],l[1]);for(let c=2;c<l.length;c+=2)e.lineTo(l[c],l[c+1]);e.stroke(a.w,s*ia)}}e.setTransform(1,0,0,1,0,0)}};var qt={freezeAfter:1.5,freezeDur:3,slide:3,minR:2.5,grow:.75,spinR:7.5,spin:.35,maxIce:200,off:!1},Ai=.3,Br=(o,e)=>1+Math.log(Math.max(1,o/(Math.max(.5,qt.minR)*e))),oa=1.5,me=(o,e)=>o+Math.random()*(e-o),Si=.05,Wt=class{constructor(){this.drops=[];this.trails=new Ot;this.woken=[];this.changed=!0;this.urgent=!0;this.fieldAt=-1/0;this.maxR=40;this.grid=new Ut;this.W=1;this.H=1;this.k=1;this.time=0;this.specks=0;this.frozen=0;this.slip=0;this.stick=1600;this.evapTau=150;this.evapEdge=2.6;this.maxSize=72}resize(e,t){e===this.W&&t===this.H||(this.W=e,this.H=t,this.k=t/1e3,this.grid.reset(e,t,70*this.k*.3),this.drops.length=0,this.trails.clear(),this.woken=[],this.urgent=!0,this.maxR=40*this.k)}spawn(e,t,r,i=1,n=.5){let a={x:e,y:t,vx:0,vy:0,mass:(r*i)**2,density:i,sx:n,sy:n,resist:0,shift:0,nextMotion:0,lastTx:e,lastTy:t,nextTrail:me(20,30)*this.k,parent:null,dead:!1,pin:null,wipe:null,awake:!1,cell:-1,wokeMass:0,wokeX:0,wokeY:0,still:0,ice:0,iceV:1,iceDur:1,rot:Math.random()*Math.PI*2,speck:!1,slid:0,tilt:0};return this.drops.push(a),this.grid.place(a),this.wake(a),a}wake(e){!e.awake&&!e.dead&&(e.awake=!0,e.wokeMass=e.mass,e.wokeX=e.x,e.wokeY=e.y,this.woken.push(e))}grew(e){e.mass>e.wokeMass*1.05&&this.wake(e)}kill(e){e.dead=!0,this.urgent=!0}size(e){return[this.sizeX(e),(e.sy+1)*Math.sqrt(Math.max(0,e.mass))/e.density]}sizeX(e){return(e.sx+1)*Math.sqrt(Math.max(0,e.mass))/e.density}step(e){if(e<=0)return;this.time+=e;let t=.01**e;for(let n=0;n<this.drops.length;n+=1)this.drops[n].dead||this.move(this.drops[n],e,t);this.collide();let r=0,i=0;for(let n of this.drops)n.speck?r+=1:n.ice>0&&(i+=1);this.specks=r,this.frozen=i;for(let n=this.drops.length-1;n>=0;n-=1){let a=this.drops[n];a.dead&&(this.endWipe(a),this.grid.unplace(a),this.drops[n]=this.drops[this.drops.length-1],this.drops.pop())}}move(e,t,r){let i=this.k;if(e.mass=Math.min(e.mass,this.massCap(e)),e.mass-=(this.evapEdge*i*Math.sqrt(Math.max(0,e.mass))+e.mass/this.evapTau)*t*(1-.85*e.ice),e.mass<=0){e.dead=!0;return}if(e.sx*=r,e.sy*=r,e.pin){e.x=e.pin.x,e.y=e.pin.y,e.vx=e.vy=0,e.still=0;return}if(this.freeze(e,t))return;if(e.vy===0&&e.mass<this.stick*i*i){e.wipe&&this.endWipe(e);return}if(this.time>=e.nextMotion){e.nextMotion=this.time+me(.1,.4);let a=(30+40*(1-this.slip))**2*i*i*4;e.resist=Math.random()*2400*i*a,e.shift=Math.random()*me(0,.1),e.mass<this.stick*i*i&&(e.resist=Math.max(e.resist,2*2400*i*e.mass))}let n=(2400*i*e.mass-e.resist)/e.mass;if(e.vy=Math.max(0,e.vy+n*t),e.vx=e.vy*e.shift,e.x+=e.vx*t,e.y+=e.vy*t,e.sy=Math.max(e.sy,.3*2*Math.atan(e.vy*.005)/Math.PI),e.y>this.H+100){e.dead=!0;return}this.grid.place(e),e.vy>8*i?(e.wipe=this.trails.track(e.wipe,e.x,e.y,this.sizeX(e)*.3),this.wake(e),this.urgent=!0):this.endWipe(e),Math.hypot(e.x-e.lastTx,e.y-e.lastTy)>e.nextTrail&&this.split(e)}freeze(e,t){let r=qt,i=this.k;if(e.speck)return e.ice=1,!0;if(e.ice===0){if(r.off||e.vy>0||this.sizeX(e)/4<r.minR*i)return e.still=0,!1;e.still+=t;let l=Br(this.sizeX(e)/4,i);if(e.still<r.freezeAfter*l||this.frozen>=r.maxIce)return!1;this.frozen+=1,e.ice=.001,e.iceV=me(.65,1.35),e.iceDur=Math.max(1,r.freezeDur*l)}if(e.ice<1&&(e.mass=Math.min(this.massCap(e),e.mass*Math.max(.25,r.grow)**(2*t/e.iceDur))),e.ice=Math.min(1,e.ice+t/e.iceDur),e.ice<Ai)return e.vx=e.vy=0,!0;let n=r.slide*i*e.iceV*Math.min(1,(e.ice-Ai)/.15);if(e.vy+=(n-e.vy)*Math.min(1,t*2),e.vx=Math.sin(this.time*.7+e.rot*5)*e.vy*.15,e.x+=e.vx*t,e.y+=e.vy*t,e.y>this.H+100)return e.dead=!0,!0;e.slid+=e.vy*t;let a=this.sizeX(e)/4/i-r.spinR,s=a>0?r.spin*Math.min(1,a/6):0;if(e.tilt+=(s*Math.sin(e.slid/(14*i)+e.rot*3)-e.tilt)*Math.min(1,t*3),this.grid.place(e),Math.abs(e.x-e.wokeX)+Math.abs(e.y-e.wokeY)>i?this.wake(e):this.grew(e),e.vy*Si>.25*i&&(this.urgent=!0),Math.hypot(e.x-e.lastTx,e.y-e.lastTy)>e.nextTrail&&e.mass>300*i*i&&this.specks<r.maxIce*oa){let l=this.spawn(e.x+me(-1.5,1.5)*i,e.y-this.sizeX(e)/4,this.sizeX(e)*me(.18,.3),1,0);l.speck=!0,l.ice=1,l.parent=e,e.mass-=l.mass,e.lastTx=e.x,e.lastTy=e.y,e.nextTrail=me(10,18)*i,this.specks+=1}return!0}split(e){if(e.mass<1e3*this.k*this.k)return;let[t,r]=this.size(e),i=this.spawn(e.x+me(-5,5)*this.k,e.y-r/4,t*me(.3,.5),.2,0);i.sx=.1,i.sy=e.vy*.01*.6,i.parent=e,e.mass-=i.mass,e.lastTx=e.x,e.lastTy=e.y,e.nextTrail=me(20,30)*this.k}endWipe(e){e.wipe&&e.wipe.end===null&&(e.wipe.end=this.time),e.wipe=null}collide(){let{cells:e,gw:t,gh:r,size:i}=this.grid,n=this.woken;this.woken=[];for(let a of n){if(a.awake=!1,a.dead)continue;let s=Math.floor(a.x/i),l=Math.floor(a.y/i),c=this.mergeDistance(a);for(let u=l-1;u<=l+1;u+=1)for(let h=s-1;h<=s+1;h+=1)if(!(h<0||u<0||h>=t||u>=r)){for(let m of e[h+u*t]){if(m===a||m.dead||m.parent===a||a.parent===m||a.parent&&a.parent===m.parent||Math.hypot(a.x-m.x,a.y-m.y)>=c+this.mergeDistance(m))continue;let[f,b]=a.mass>=m.mass?[a,m]:[m,a],p=f.mass+b.mass;if(f.vx=(f.vx*f.mass+b.vx*b.mass)/p,f.vy=(f.vy*f.mass+b.vy*b.mass)/p,f.density=(f.density*f.mass+b.density*b.mass)/p,f.mass=Math.min(p,this.massCap(f)),f.ice=f.ice>0?(f.ice*(p-b.mass)+b.ice*b.mass)/p:0,f.ice<.001&&(f.ice=0),f.speck=f.speck&&b.speck,b.dead=!0,this.urgent=!0,this.wake(f),b===a)break}if(a.dead)break}}}absorb(e,t){this.grid.alongSegments(e,t,this.maxR,r=>this.sizeX(r)/4,r=>this.kill(r))}near(e,t,r){return this.grid.at(e,t,r,i=>this.sizeX(i)/4)}massCap(e){return(this.maxSize*this.k*e.density)**2}mergeDistance(e){return this.sizeX(e)*(1+e.sx)*.16}drawFog(e,t,r){this.trails.draw(e,this.time,t,r)}instances(e){if(this.changed=this.urgent||this.time-this.fieldAt>=Si,!this.changed)return e;this.urgent=!1,this.fieldAt=this.time,e.length=0;let t=0;for(let r of this.drops){let i=Math.sqrt(Math.max(0,r.mass))/r.density,n=(r.sx+1)*i,a=(r.sy+1)*i;n<2||r.dead||(e.push(r.x,r.y,n*2,a*2,n/50,1),t=Math.max(t,n/4))}return this.maxR=t+2*this.k,e}};var _r=6,aa=o=>{let e=Math.sin(o*12.9898+78.233)*43758.5453;return e-Math.floor(e)},sa=(o,e,t,r,i,n,a)=>{o.push(e,t+r*.42,r*1.05,i,n,a)},Ht=(o,e,t,r,i,n,a,s=1,l=1)=>{let c=o.ice,u=qt;if(!c||u.off||n<0||r<u.minR*c.k){o.motion.put(e,t,r,i,s,l);return}let h=aa(a),m=c.k,f=Br(r,m),b=n-u.freezeAfter*f*(.7+.6*h);if(b<=0){o.motion.put(e,t,r,i,s,l);return}let p=Math.max(1,u.freezeDur*f),y=Math.min(1,b/p),g=r*Math.max(.25,u.grow)**y,d=Math.min(2*m,.5*m*Math.max(0,g/(u.minR*m)-1))*y*y*(3-2*y);o.motion.put(e,t+d,g,i,s,l),sa(c.out,e,t+d,g,y,h*Math.PI*2,i)};var Ir=(o,e,t,r,i,n,a,s)=>{if(!(r-t<2))for(let l=t;l<r;l+=4){let c=Math.min(r,l+5),u=(l-t)/Math.max(1,r-t),h=i*(.72+.28*u)*(.85+.3*(.5+.5*Math.sin(l*.19+a*3)));o.moveTo(e+s(l),l),o.lineTo(e+s(c),c),o.stroke(h,n)}},la=(o,e,t)=>{let{pts:r,cum:i}=e;o.moveTo(r[0][0],r[0][1]);for(let n=1;n<r.length;n+=1){if(i[n]>=t){let a=(t-i[n-1])/Math.max(1e-6,i[n]-i[n-1]);o.lineTo(r[n-1][0]+(r[n][0]-r[n-1][0])*a,r[n-1][1]+(r[n][1]-r[n-1][1])*a);return}o.lineTo(r[n][0],r[n][1])}},Le=(o,e,t)=>{let r=Math.cos(o.rot),i=Math.sin(o.rot),n=(e-.5)*o.s,a=(t-.5)*o.s;return[o.x+r*n-i*a,o.y+i*n+r*a]},ca=24,ua=(o,e,t,r,i)=>{let[n,a]=we(t,r),[s,l]=we(t,Math.max(0,r-ca/e.s)),[c,u]=Le(e,n,a),[h,m]=Le(e,s,l);o.push(h,m,c,u,i*e.s)},ha=(o,e,t,r,i)=>{let{pts:n,cum:a}=t,[s,l]=Le(e,n[0][0],n[0][1]);for(let c=1;c<n.length&&a[c-1]<r;c+=1){let[u,h]=a[c]>r?we(t,r):n[c],[m,f]=Le(e,u,h);o.push(s,l,m,f,i*e.s),s=m,l=f}},ma=(o,e,t,r,i,n,a,s,l)=>{let c=0,u=-1e9;for(let x of t.pts){let[E,v]=Le(e,x[0],x[1]);v>u&&(c=E,u=v)}u+=n*e.s*.35;let h=S(i/r.dur),m=h-Math.sin(h*6*Math.PI)/(6*Math.PI),f=r.len*m,b=r.r*(1-.35*h),p=x=>Math.sin(x*.07+r.seed)*1.6+Math.sin(x*.023+r.seed*2)*2.5+Ze(x,o.busSpeed);if(f>1&&a>.002&&Ir(o.ctx,c,u,u+f,b*1.5,a*.95,r.seed,x=>p(x-u)),s<=.002)return;let y=1-Math.cos(h*6*Math.PI);for(let x=14+r.seed%7*2;x<f-b*2;x+=18+(x*7+r.seed*13)%11){let E=c+p(x),v=l?s*l(E,u+x):s;v>.002&&o.motion.put(E,u+x,b*(.3+.2*((x+r.seed)*.37%1)),v)}let g=c+p(f),d=u+f+b*.3,w=l?s*l(g,d):s;if(w>.002&&Ht(o,g,d,b,w,h<1?-1:i-r.dur,r.seed+.5,1,1+.35*y*(h<1?1:0)),o.sweeps&&h<1&&f>1&&s>.3){let x=Math.max(0,f-b*3);o.sweeps.push(c+p(x),u+x,c+p(f),u+f+b*.3,b*2.4)}},Nr=(o,e,t,r,i=r,n=null)=>{let a=e.glyph,s=e.appearDur>0;if(!a||t<(s?e.t:e.wt))return;let{ctx:l,motion:c}=o,u=r,h=i;if(s){let v=N(S((t-e.t)/e.appearDur));u*=v,h*=v}let m=e.wd,f=Math.min(.06/o.writeSpeed,m*.08),b=m-f*(a.strokes.length-1),p=t-(s?e.t:e.wt),y=q,g=Math.cos(e.rot),d=Math.sin(e.rot);l.setTransform(g*e.s*y,d*e.s*y,-d*e.s*y,g*e.s*y,(e.x-e.s/2*(g-d))*y,(e.y-e.s/2*(d+g))*y);let w=S(Math.sqrt(9/a.strokes.length),.55,1),x=Math.max(o.strokeWidth*w,.6/(q*e.s)),E=[];a.strokes.forEach((v,A)=>{let M=s?0:b*v.len/a.total;if(p<=0)return;let T=M>0?S(p/M):1,F=(T*.4+N(T)*.6)*v.len;if(u>.002&&(la(l,v,F),l.stroke(x,u)),o.sweeps&&!s&&T>0&&T<1&&ua(o.sweeps,e,v,F,x),o.wet&&!s&&F>0&&h>.3&&ha(o.wet,e,v,F,x),!s){for(let L of e.beads[A]){if(L.s>F)break;let P=x/2+(L.r*.7+L.j*1.5)/e.s,[D,U]=Le(e,L.x+L.nx*P,L.y+L.ny*P),j=n&&h>.002?h*n(D,U):h;j<=.002||Ht(o,D,U,L.r*(.55+.45*j),j,T>=1?p-M:-1,L.x*131.7+L.y*71.3+A,1+L.j*.25,1+(1-L.j)*.3)}if(_.endBead>0&&F>0&&e.s>=_.endBeadMin){let L=e.ends[A],[P,D]=we(v,F),[U,j]=we(v,Math.max(0,F-2/e.s)),z=P-U,Z=D-j,se=Math.hypot(z,Z)||1;z/=se,Z/=se;let V=e.s*(.031+.018*L.h)*_.endBead*(.5+.5*N(T)),[ie,ne]=Le(e,P+z*V*.35/e.s,D+(Z*V*.35+V*.15)/e.s),k=n&&h>.002?h*n(ie,ne):h;k>.002&&Ht(o,ie,ne,V*(.6+.4*k),k,T>=1?p-M:-1,e.x*3.1+e.y*5.7+A*11,1,1.12+L.j*.12)}let R=e.drips[A];R&&T>=1&&p-M>R.delay&&E.push([v,R,p-M-R.delay])}p-=M+(s?0:f)}),l.setTransform(q,0,0,q,0,0);for(let[v,A,M]of E)ma(o,e,v,A,M,x,u,h,n);l.setTransform(1,0,0,1,0,0)};var Mi=(o,e)=>{let t=W(e*4241+77>>>0),{x0:r,x1:i,y0:n,y1:a,S:s}=Fe(o);if(!Number.isFinite(r))return null;let l=Math.max(14,s*.28),c=2,u=l*.8,h=u+l,m=(n+a)/2,f=s*.3,b=[{w:l*(.92+t()*.08),lenK:.88+t()*.04},{w:l*(.97+t()*.08),lenK:1}],p=t()<.5?1:-1,y=t()*Math.PI*2,g=.45+t()*.15,d=a-n+f*2,w=Math.sin(g)*d*.5,x=r-f-w,E=i+f+w,v=Math.max(6,Math.ceil((E-x)/(h*.5))),A=[];for(let T=0;T<v;T+=1){let F=x+(E-x)*(T+.5)/v,R=T%2===0?1:-1,L=Math.floor(T/2),P=g*Math.sin(y+L*.5)+(t()-.5)*.06,D=Math.cos(P),U=Math.sin(P),j=d*(.95+t()*.1),z=p*s*(.08+t()*.03),Z=T/v*.96,se=Math.min(1,(T+1)/v*.96+.04);for(let V=0;V<c;V+=1){let ie=(V-(c-1)/2)*u,{w:ne,lenK:k}=b[V],C=[];for(let Y=0;Y<=12;Y+=1){let Pe=Y/12,De=2*Pe-1,le=R*(Pe-.5)*j*k,oe=ie+z*(1-De*De);C.push([F+U*le+D*oe,m+D*le-U*oe])}let O=ne*.7,B=C.map(Y=>Y[0]),G=C.map(Y=>Y[1]);A.push({pts:C,w:ne,a:Z,b:se,box:[Math.min(...B)-O,Math.min(...G)-O,Math.max(...B)+O,Math.max(...G)+O]})}}let M=[];for(let T of A.slice(-c)){let[F,R]=T.pts[T.pts.length-1];M.push({x:F,y:R,r:T.w*.22+1.5*t()}),t()<.6&&M.push({x:F-(T.pts[1][0]-T.pts[0][0])*.6+(t()-.5)*T.w*.6,y:R-(T.pts[1][1]-T.pts[0][1])*.6,r:1.5+t()*2.5})}return{streaks:A,beads:M,dur:Math.min(.08*v,1.6)}},Pr=(o,e)=>S((e-o.a)/(o.b-o.a)),Ti=(o,e,t,r)=>o.streaks.some(i=>{let n=Pr(i,e);if(n<=0)return!1;let a=i.box;if(t<a[0]||t>a[2]||r<a[1]||r>a[3])return!1;let s=i.pts,l=n*(s.length-1),c=i.w*.7,u=c*c;for(let h=0;h<l;h+=1){let[m,f]=s[h],b=s[h+1],p=Math.min(1,l-h),y=(b[0]-m)*p,g=(b[1]-f)*p,d=S(((t-m)*y+(r-f)*g)/(y*y+g*g||1)),w=t-m-y*d,x=r-f-g*d;if(w*w+x*x<u)return!0}return!1});var fa=8,ba=2.5,pa=.05,zt=class{constructor(){this.ctx=new vt;this.breath=new Gt;this.H=1;this.sweeps=null;this.wet=null;this.ice=null;this.wetAt=-1/0}resize(e,t){this.H=t,this.breath.resize(e,t)}draw(e,t,r,i,n,a,s){let{ctx:l}=this;l.clear(),this.breath.begin(),this.sweeps&&(this.sweeps.length=0),this.ice&&(this.ice.length=0);let c=!!this.wet&&r-this.wetAt>=pa;c&&(this.wet.length=0,this.wetAt=r);let u={ctx:l,motion:n,strokeWidth:i.strokeWidth,writeSpeed:i.writeSpeed,busSpeed:i.busSpeed,sweeps:this.sweeps,wet:c?this.wet:null,ice:this.ice?{out:this.ice,k:this.H/1e3,H:this.H}:null};e.forEach((h,m)=>{h.breathAt<1/0&&this.breath.drawLine(h,m,t,h.breathAt,h.breathUntil,h.breathDur,!0),h.appearBreathAt<1/0&&this.breath.drawLine(h,m,t,h.appearBreathAt,h.appearBreathUntil,_.breathSpread,!1),t>=h.t-.5&&this.drawLine(u,h,m,t,i)}),this.breath.end(),a.draw(l,n,t,i.refog,_.beadHold,s),this.drawTrails(n,r,i)}drawLine(e,t,r,i,n){let{refog:a}=n,s=_.beadHold,{fogAt:l}=t,c=Lr(t,i),u=gi(t,i);if(t.mode==="fog"||t.breathAt<1/0){let E=(1-N(S((i-l)/a)))*u,v=(1-N(S((i-l-s)/(a*3))))*c;if(v>.002||E>.002)for(let A of t.chars)Nr(e,A,i,E,v);return}t.wipe===void 0&&(t.wipe=Mi(t,r));let h=t.wipe;if(!h)return;let m=t.yieldAt-h.dur-.9,f=l>m&&m>=t.end?m:l,b=S((i-f)/h.dur),p=1-N(S((i-f-h.dur-.5)/a)),y=p*u,g=p*c,d=(1-N(S((i-f-h.dur-.5-s)/(a*3))))*c;if(d<=.002&&y<=.002)return;let w=b<=0?null:(E,v)=>Ti(h,b,E,v)?0:1;for(let E of t.chars)Nr(e,E,i,b<=0?u:y,b<=0?c:d,w);if(b<=0)return;let{ctx:x}=e;if(x.setTransform(q,0,0,q,0,0),g>.002)for(let E of h.streaks){let v=Pr(E,b);if(v<=0)continue;let A=E.pts,M=A.length-1,T=v*M,F=Math.floor(T);x.moveTo(A[0][0],A[0][1]);for(let R=1;R<=Math.min(F,M);R+=1)x.lineTo(A[R][0],A[R][1]);if(F<M){let R=A[F],L=A[F+1],P=T-F;x.lineTo(R[0]+(L[0]-R[0])*P,R[1]+(L[1]-R[1])*P)}if(x.stroke(E.w,g),e.sweeps&&v<1){let[R,L]=A[Math.max(0,F-1)],P=A[Math.min(M,F+1)];e.sweeps.push(R,L,P[0],P[1],E.w)}}if(x.setTransform(1,0,0,1,0,0),b>=1&&d>.002)for(let E of h.beads)e.motion.put(E.x,E.y,E.r*(.55+.45*d),d)}drawTrails(e,t,r){let{ctx:i}=this,n=e.smears;i.setTransform(q,0,0,q,0,0);for(let s=n.length-1;s>=0;s-=1){let l=n[s],c=l.end===null?1:1-N(S((t-l.end-fa)/(r.refog*ba)));if(c<=.002){l.gone=!0,n.splice(s,1);continue}i.moveTo(l.x+l.ax,l.y+l.ay),i.lineTo(l.x+l.bx,l.y+l.by),i.stroke(l.w*.14,c*.9)}let a=e.trails;for(let s=a.length-1;s>=0;s-=1){let l=a[s],c=l.end===null?1:1-N(S((t-l.end-r.hold)/r.refog));if(c<=.002){a.splice(s,1);continue}if(l.y1-l.y0<2||l.y0>this.H)continue;i.setTransform(q,0,0,q,0,0);let u=h=>Math.sin(h*.07+l.seed)*1.4+Math.sin(h*.021+l.seed*2)*2.2+Ze(h-l.y0,r.busSpeed);Ir(i,l.x,l.y0,l.y1,l.w*.18,c*.95,l.seed,u);for(let h=l.y0+10+l.seed%9;h<l.y1-l.w*.25;h+=16+(h*7+l.seed*13)%13)e.put(l.x+u(h),h,l.w*(.03+.03*((h+l.seed)*.37%1)),c)}i.setTransform(1,0,0,1,0,0)}};var Dr=(o,e)=>{let t=o?cn(o):null;return t?[t.r/255,t.g/255,t.b/255]:e},Ve=([o,e,t])=>{let r=Math.max(o,e,t),i=Math.min(o,e,t),n=(r+i)/2,a=r-i;if(a===0)return[0,0,n];let s=a/(1-Math.abs(2*n-1));return[(r===o?((e-t)/a+6)%6:r===e?(t-o)/a+2:(o-e)/a+4)/6,s,n]},Vt=([o,e,t])=>{let r=a=>(a+o*12)%12,i=e*Math.min(t,1-t),n=a=>t-i*Math.max(-1,Math.min(r(a)-3,9-r(a),1));return[n(0),n(8),n(4)]},Cr=o=>{let[e,t,r]=Ve(o);return Vt([e,Math.max(.55,t),Math.min(.78,Math.max(.6,r))])},Ri=(o,e)=>{let[t,r]=Ve(o);return Vt([t,Math.min(.55,r),e])},Fi=o=>{let e=Dr(o.accentColor,[1,.81,.54]),t=Dr(o.backgroundColor,[.04,.06,.09]),r=[o.secondaryColor,o.primaryColor,...(o.wordColors??[]).map(c=>c.color)].map(c=>Dr(c,[0,0,0])).filter(c=>Ve(c)[1]>.3&&Ve(c)[2]>.25&&Ve(c)[2]<.9),[i,n,a]=Ve(e),s=r[0]??Vt([(i+.55)%1,n,a]),l=r[1]??Vt([(i+.9)%1,n,a]);return{sky0:Ri(t,.05),sky1:Ri(t.map((c,u)=>c*.7+e[u]*.3),.14),la:Cr(e),lb:Cr(s),lc:Cr(l)}};var ke=o=>o.appearDur>0?o.t:o.wt,da=o=>ke(o)+(o.appearDur>0?o.appearDur:o.wd),Li=new WeakMap,xa=o=>{let e=Li.get(o);if(e===void 0){e=o.end;for(let t of o.chars)t.glyph&&(e=Math.max(e,da(t)));Li.set(o,e)}return e},ya=(o,e)=>{for(let t=o.length-1;t>=0;t-=1){let r=o[t];if(e<r.t-.05||e>xa(r)+.8)continue;let i=-1;if(r.chars.forEach((a,s)=>{a.glyph&&ke(a)<=e&&(i<0||ke(a)>=ke(r.chars[i]))&&(i=s)}),i<0)continue;let n=null;for(let a=i+1;a<r.chars.length&&!n;a+=1)r.chars[a].glyph&&(n=r.chars[a]);return{c:r.chars[i],next:n}}return null},Xt=class{constructor(){this.x=0;this.y=0;this.r=0;this.i=0;this.vx=0;this.vy=0;this.vr=0;this.t=0;this.init=!1}update(e,t,r){this.t+=r,Math.abs(t-this.t)>.25||r===0?this.t=t:this.t+=(t-this.t)*(1-Math.exp(-r/.3));let i=ya(e,this.t);if(i){let{c:n,next:a}=i,s=a?N(S((this.t-ke(n))/Math.max(.05,ke(a)-ke(n)))):0,l=a?n.x+(a.x-n.x)*s:n.x,c=a?n.y+(a.y-n.y)*s:n.y,u=a?n.s+(a.s-n.s)*s:n.s;(!this.init||r===0)&&(this.x=l,this.y=c,this.r=u,this.vx=this.vy=this.vr=0,this.init=!0);let h=1/.1,m=Math.ceil(r/.008);for(let f=0;f<m;f+=1){let b=r/m;this.vx+=(h*h*(l-this.x)-2*h*this.vx)*b,this.x+=this.vx*b,this.vy+=(h*h*(c-this.y)-2*h*this.vy)*b,this.y+=this.vy*b,this.vr+=(h*h*(u-this.r)-2*h*this.vr)*b,this.r+=this.vr*b}}r===0?this.i=i?1:0:this.i+=((i?1:0)-this.i)*(1-Math.exp(-r/(i?.15:.5)))}reset(){this.init=!1,this.i=0}};var va=.35,wa=.3,Ea=.5,Kt=class{constructor(){this.list=null;this.key=null;this.next=0;this.lastT=-1/0;this.lastBump=-1/0}step(e,t,r){if(e!==this.list||t!==this.key)return this.list=e,this.key=t,this.seek(r),0;if(r<this.lastT||r-this.lastT>Ea)return this.seek(r),0;let i=0,{times:n,strengths:a,kinds:s}=e;for(;this.next<n.length&&n[this.next]<=r;this.next+=1){let l=n[this.next];s[this.next]!==0||a[this.next]<va||l-this.lastBump<wa||(this.lastBump=l,i=Math.max(i,.3+.7*a[this.next]))}return this.lastT=r,i}reset(){this.list=null,this.key=null}seek(e){let t=this.list?.times,r=0,i=t?.length??0;for(;r<i;){let n=r+i>>1;t[n]<=e?r=n+1:i=n}this.next=r,this.lastT=e,this.lastBump=-1/0}};var Gr=$e.nebbiaDrizzleRide,Aa=xe(Gr,"rain"),Sa=xe(Gr,"bump"),jt=class{constructor(){this.drumBumps=new Kt;this.env=null;this.rain=1}step(e,t,r,i,n,a=!0){let s=ht(Gr),l=s?mt().key:null,c=s&&a?nn(l):null;if(e.beginFrame(t,i,a&&!c),c&&!i){let u=this.drumBumps.step(c,l,r),h=ye(Sa);u>0&&h>0&&n>0&&e.trigger(t,u*h)}else c||this.drumBumps.reset();this.rain=s?ye(Aa):1,this.env=s?vi(l):null}};var Ma={buildings:1,lights:2,cars:4,road:8},Ta={slant:48,straight:16,off:0},ki=(o,[e,t],r,i,n,a,s=null)=>{let l=i/n,c=s?-.04:.595;return{time:a,mist:o.mist,edge:_.edge,refract:_.refract,bus:o.busSpeed,glint:_.glint,neon:o.neon,glassShift:[-e*.35/i,-t*.35/n],sceneShift:[-e/i,-t/n],spotSrc:[o.spotX*l,c],spotSrc2:[(1-o.spotX)*l,c],spotTgt:[r.x/i*l,r.y/n],spotR:Math.max(.02,r.r/n*.75)*(s?.spotScale??1),spotI:o.scenery.includes("spot")?r.i*o.spot:0,scenery:o.scenery.reduce((u,h)=>u|(Ma[h]??0),o.glass==="snow"&&!s?0:Ta[o.rain]),dewGlint:o.dewGlint,aqua:s}};var Ra=1e3,Fa=3600,Yt=class{constructor(){this.time=0;this.last=0}tick(e,t){let r=t?0:S(e-this.last,0,.1);return this.time=(this.time+r)%Fa,this.last=e,r}songChanged(){this.time>Ra&&(this.time=0)}};var La=81,Bi=1.5,ka=10,nt=6,Ur=4,Ba=12.5,_a=25,_i=.05,Ee=24,Ii=2,Ia=4,$t=class{constructor(){this.list=[];this.inst=new Float32Array(0);this.rng=W(9191);this.W=0;this.H=0;this.time=0;this.sinceUpdate=0;this.lastSize=-1;this.lastCount=-1;this.dirty=!0;this.changed=!0;this.cells=[];this.cellOf=new Int32Array(0);this.gw=1;this.wetAt=-1/0}build(e,t){this.W=e,this.H=t,this.time=0,this.wetAt=-1/0;let r=Math.round(e*t/La*Bi);this.list=Array.from({length:r},()=>this.spawn(this.rng()*ka)),this.inst=new Float32Array(r*6),this.gw=Math.ceil(e/Ee)+1,this.cells=Array.from({length:this.gw*(Math.ceil(t/Ee)+1)},()=>[]),this.cellOf=new Int32Array(r),this.list.forEach((i,n)=>{this.cellOf[n]=this.cellAt(i.x,i.y),this.cells[this.cellOf[n]].push(n)}),this.dirty=!0}cellAt(e,t){let r=this.cells.length/this.gw;return Math.min(this.gw-1,Math.max(0,Math.floor(e/Ee)))+Math.min(r-1,Math.max(0,Math.floor(t/Ee)))*this.gw}touchInk(e,t,r){e.length&&this.wipe(e,0,r),!(!t.length||this.time-this.wetAt<_i)&&(this.wetAt=this.time,this.wipe(t,2,r))}wipe(e,t,r=1){let{gw:i,cells:n,list:a,time:s}=this,l=n.length/i;for(let c=0;c<e.length;c+=5){let u=e[c+4]/2+t+5*r,h=Math.max(0,Math.floor((Math.min(e[c],e[c+2])-u)/Ee)),m=Math.min(i-1,Math.floor((Math.max(e[c],e[c+2])+u)/Ee)),f=Math.max(0,Math.floor((Math.min(e[c+1],e[c+3])-u)/Ee)),b=Math.min(l-1,Math.floor((Math.max(e[c+1],e[c+3])+u)/Ee));for(let p=f;p<=b;p+=1)for(let y=h;y<=m;y+=1)for(let g of n[y+p*i]){let d=a[g];g>=this.lastCount||d.born>s+Ii*.5||Se(d.x,d.y,e,c)>=e[c+4]/2+t+d.r*r||(d.born<=s&&(d.full=!1),d.born=s+Ii+this.rng()*Ia)}}}spawn(e){let{rng:t}=this,r=this.H*(1-t()**1.4),i=r/Math.max(1,this.H),n=t()<.06+.08*i?3+t()*(.55+.7*i):1.2+t()**1.5*(1.35+.4*i);return{x:t()*this.W,y:r,r:n,sy:1+t()*.2,born:e,life:Ba+t()*_a,full:!1}}instances(e,t=1,r=1){let i=this.inst;this.time+=Math.min(e,.1),this.sinceUpdate+=Math.min(e,.1);let n=this.time,a=Math.min(this.list.length,Math.round(this.list.length*t/Bi)),s=r!==this.lastSize,l=this.dirty||s||a!==this.lastCount;if(!l&&this.sinceUpdate<_i)return this.changed=!1,i.subarray(0,a*6);if(this.sinceUpdate=0,s)for(let c of this.list)c.full=!1;for(let c=0;c<a;c+=1){let u=this.list[c];if(n-u.born>nt+u.life+Ur){u=this.list[c]=this.spawn(n+this.rng()*3);let d=this.cellAt(u.x,u.y),w=this.cells[this.cellOf[c]];w.splice(w.indexOf(c),1),this.cells[d].push(c),this.cellOf[c]=d}let h=n-u.born,m=h>=nt&&h<=nt+u.life;if(m&&u.full)continue;let f=Math.min(1,Math.max(0,h/nt)),b=Math.min(1,Math.max(0,(nt+u.life+Ur-h)/Ur)),p=u.r*r*Math.sqrt(f)*Math.sqrt(b),y=p<.5?0:p*8,g=c*6;i[g]=u.x,i[g+1]=u.y,i[g+2]=y,i[g+3]=y*u.sy,i[g+4]=y/100,i[g+5]=1,u.full=m,l=!0}return this.lastSize=r,this.lastCount=a,this.dirty=!1,this.changed=l,i.subarray(0,a*6)}};var Or=5,Na=(o,e,t)=>{let r=o.x,i=o.y,n=o.r,a=1,s=1,l=.92,c=1,u=Math.min(1,o.age/o.app);if(u<1){let h=(1-u)**1.2,m=Math.sin(Math.PI*u)*o.curl,f=o.fa*h;r+=o.ox*h-o.oy*m+Math.sin(o.age*o.ff+o.fp)*f,i+=o.oy*h+o.ox*m+Math.cos(o.age*o.ff*.7+o.fp)*f;let b=1/(1+(Or-1)*(1-u));n*=b,c=(b-1/Or)/(1-1/Or),l*=Math.min(1,u/.15)*(.35+.65*c)}else{let h=Math.exp(-(o.age-o.app)*14);a=1+.07*h,s=1-.08*h,i-=o.r*.1*o.melt}e[t]=r,e[t+1]=i,e[t+2]=n,e[t+3]=o.rot,e[t+4]=Math.min(o.melt*15,16-1.001),e[t+5]=o.kind.row,e[t+6]=l,e[t+7]=c,e[t+8]=a,e[t+9]=s},Ni=(o,e,t=0)=>{let r=t+o.length;e.length<r*Q&&(e=new Float32Array(Math.ceil(r*1.5)*Q));for(let i=0;i<o.length;i+=1)Na(o[i],e,(t+i)*Q);return e},Pi=(o,e,t,r,i)=>{let n=0;for(let s of o)s.ice>0&&!s.dead&&(n+=1);if(r.length<(i+n)*Q){let s=new Float32Array(Math.ceil((i+n)*1.5)*Q);s.set(r.subarray(0,i*Q)),r=s}let a=i*Q;for(let s of o){if(!(s.ice>0)||s.dead)continue;let l=t[Math.floor(s.rot*10)%t.length],c=e(s);r[a]=s.x,r[a+1]=s.y+c*(.42+.2*s.sy),r[a+2]=c*1.05,r[a+3]=s.rot+s.tilt,r[a+4]=Math.min((1-s.ice)*15,16-1.001),r[a+5]=l.row,r[a+6]=.9*Math.min(1,s.ice*5),r[a+7]=1,r[a+8]=1+s.sx*.5,r[a+9]=1+s.sy*.5,a+=Q}return[r,n]},Di=(o,e,t,r)=>{let i=o.length/_r;if(t.length<(r+i)*Q){let n=new Float32Array(Math.ceil((r+i)*1.5)*Q);n.set(t.subarray(0,r*Q)),t=n}for(let n=0;n<i;n+=1){let a=n*_r,s=(r+n)*Q,l=o[a+3];t[s]=o[a],t[s+1]=o[a+1],t[s+2]=o[a+2],t[s+3]=o[a+4],t[s+4]=Math.min((1-l)*15,16-1.001),t[s+5]=e[Math.floor(Math.abs(o[a+4])*10)%e.length].row,t[s+6]=.9*Math.min(1,l*5)*o[a+5],t[s+7]=1,t[s+8]=1,t[s+9]=1}return[t,i]};var Jt=[{n:240,r:[.9,1.7],vy:[45,75],alpha:.3,p:.2,sway:[1,3]},{n:70,r:[2,3.2],vy:[95,140],alpha:.5,p:.55,sway:[2,5]}],it=(o,e)=>o+Math.random()*(e-o),Pa=o=>o*(1+.5*(o-1)),Wr=o=>-.45*Math.max(0,Pa(o)),Qt=class{constructor(e){this.rows=e;this.layers=Jt.map(()=>[]);this.W=0;this.H=0;this.k=1}resize(e,t){this.W=e,this.H=t,this.k=t/1e3;for(let r of this.layers)r.length=0}get count(){return this.layers[0].length+this.layers[1].length}step(e,t,r){let{W:i,H:n,k:a}=this,s=Wr(r),l=i*n/(1280*720);Jt.forEach((c,u)=>{let h=this.layers[u],m=Math.round(c.n*t*l);for(;h.length<m;)h.push(this.make(u,Math.random()*n));h.length>m&&(h.length=m);let f=20*a;for(let b of h)b.y+=b.vy*e,b.x+=b.vy*s*e,b.rot+=b.spin*e,b.y>n+f&&Object.assign(b,this.make(u,-f)),b.x<-f?b.x+=i+2*f:b.x>i+f&&(b.x-=i+2*f)})}make(e,t){let r=Jt[e],i=this.k;return{x:Math.random()*this.W,y:t,r:it(r.r[0],r.r[1])*i,rot:Math.random()*Math.PI*2,spin:it(-.6,.6),row:Math.floor(Math.random()*this.rows),vy:it(r.vy[0],r.vy[1])*i,sa:it(r.sway[0],r.sway[1])*i,sf:it(.8,1.6),sp:Math.random()*Math.PI*2}}pack(e,t){let r=0;Jt.forEach((i,n)=>{for(let a of this.layers[n])e[r]=a.x+Math.sin(t*a.sf+a.sp)*a.sa,e[r+1]=a.y,e[r+2]=a.r,e[r+3]=a.rot,e[r+4]=0,e[r+5]=a.row,e[r+6]=i.alpha,e[r+7]=i.p,e[r+8]=1,e[r+9]=1,r+=Q})}};var qr=5,Hr=["clump","clump","clump","clump","clump","clump","clump","star","star","star","star","star"],Ci=[["clump",.6],["star",1]],Ga=.25,fe=(o,e)=>o+Math.random()*(e-o),Gi=(o,e)=>(4*4.5*Math.log(1+o/e/2.5)*e)**2,Ui=Math.PI*2,Zt=class{constructor(){this.kinds=Hr.map(yr);this.beads=[0,1,2].map(e=>yr("bead",Hr.length+e));this.byType=new Map(Ci.map(([e])=>[e,this.kinds.filter(t=>t.type===e)]));this.inst=new Float32Array(0);this.water=new Wt;this.fall=new Qt(Hr.length);this.flakes=[];this.spawnAcc=0;this.W=0;this.H=0;this.k=1;this.clock=0;this.wetAt=-1/0;this.sweepBox=[0,0,0,0]}resize(e,t){e===this.W&&t===this.H||(this.W=e,this.H=t,this.k=t/1e3,this.water.resize(e,t),this.fall.resize(e,t),this.flakes=[])}atlas(){let e=[...this.kinds,...this.beads],t=Jn(e);for(let r of e)r.stages=[];return[t,e.length]}step(e,t,r,i,n,a=!0){for(this.fall.step(e,a?t.snow:0,n),this.spawnAcc+=e*t.snow*25*this.W*this.H/(1280*720);this.spawnAcc>=1;this.spawnAcc-=1)this.flakes.push(this.newFlake(t,n));this.boxOf(r);for(let s=this.flakes.length-1;s>=0;s-=1){let l=this.flakes[s],c=l.age<l.app;if(l.age+=e,l.age<l.app){l.rot+=l.spin*(1-l.age/l.app)*e;continue}c&&this.landsInWater(l,r,i)&&(l.quick=!0),l.quick&&(l.melt=Math.min(1,l.melt+e/Ga)),(l.quick?l.melt>=1:this.wiped(l,r)||l.age>=l.app+l.hold&&this.melt(l,e*t.snowMelt))&&(this.flakes[s]=this.flakes[this.flakes.length-1],this.flakes.pop())}this.water.step(e),this.clock+=e,r.length&&this.water.absorb(r,0),i.length&&this.clock-this.wetAt>=.05&&(this.wetAt=this.clock,this.water.absorb(i,3*this.k))}newFlake(e,t){let{k:r}=this,i=Math.random(),n=Ci.find(([,h])=>i<h)[0],a=this.byType.get(n),s=(Math.random()<.3?fe(4.5,6):fe(2.5,4.5))*r*e.snowSize,l=fe(2,3.2)/(.75+.25*t),c=fe(45,75)*r*l,u=1/(.6+.4*t);return{x:Math.random()*this.W,y:Math.random()*this.H,r:s,rot:Math.random()*Ui,spin:fe(-1.33,1.33)*(.7+.3*t),kind:a[Math.floor(Math.random()*a.length)],app:l,ox:fe(-15,15)*r-Wr(t)*c,oy:-c,curl:fe(-.2,.2)*u,ff:fe(1.5,3.5),fa:fe(1,4)*r*u,fp:Math.random()*Ui,age:0,hold:.3+Math.random()**2.5*7,dur:fe(2.5,4.5)+s/r*.18,melt:0,water:0,drops:null,quick:!1}}melt(e,t){e.melt=Math.min(1,e.melt+t/e.dur);let r=Gi(e.r,this.k)*e.melt,i=r-e.water;if(e.water=r,!e.drops&&e.melt>.12)e.drops=e.kind.lumps.map(n=>{let[a,s]=this.lumpAt(e,n),l=this.water.spawn(a,s,Math.sqrt(Math.max(1,r*n.w)),1,0);return l.pin={x:a,y:s},l});else if(e.drops){let n=e.kind.lumps,a=e.drops,s=0;for(let l=0;l<a.length;l+=1){let c=a[l];c?.dead?a[l]=null:c&&(s+=n[l].w)}for(let l=0;l<a.length;l+=1){let c=a[l];c&&(c.mass+=i*n[l].w/(s||1),this.water.grew(c))}}if(e.melt<1)return!1;for(let n of e.drops??[])n&&(n.pin=null);return!0}lumpAt(e,t){let r=e.r*2/(64*.84)*26.88,i=Math.cos(e.rot),n=Math.sin(e.rot);return[e.x+(t.x*i-t.y*n)*r,e.y+(t.x*n+t.y*i)*r+e.r*.25]}landsInWater(e,t,r){if(this.touches(e,t)||this.touches(e,r))return!0;let i=this.water.near(e.x,e.y,e.r*.6);return i&&(i.mass+=Gi(e.r,this.k),this.water.wake(i)),i!==null}touches(e,t){for(let r=0;r<t.length;r+=qr)if(Se(e.x,e.y,t,r)<t[r+4]/2+e.r*.6)return!0;return!1}boxOf(e){let t=this.sweepBox;t[0]=t[1]=1/0,t[2]=t[3]=-1/0;for(let r=0;r<e.length;r+=qr){let i=e[r+4]/2;t[0]=Math.min(t[0],e[r]-i,e[r+2]-i),t[1]=Math.min(t[1],e[r+1]-i,e[r+3]-i),t[2]=Math.max(t[2],e[r]+i,e[r+2]+i),t[3]=Math.max(t[3],e[r+1]+i,e[r+3]+i)}}wiped(e,t){let r=this.sweepBox,i=e.r*.6;if(e.x+i<r[0]||e.x-i>r[2]||e.y+i<r[1]||e.y-i>r[3])return!1;for(let n=0;n<t.length;n+=qr)if(!(Se(e.x,e.y,t,n)>t[n+4]/2+e.r*.6)){for(let a of e.drops??[])a&&this.water.kill(a);return!0}return!1}drawFog(e,t){this.water.drawFog(e,t.hold,t.refog)}instances(e){return this.water.instances(e)}get changed(){return this.water.changed}flakeInstances(e=[]){let t=this.fall.count;this.inst=Ni(this.flakes,this.inst,t),this.fall.pack(this.inst,this.clock);let r=t+this.flakes.length,i;return[this.inst,i]=Pi(this.water.drops,n=>this.water.sizeX(n)/4,this.beads,this.inst,r),r+=i,[this.inst,i]=Di(e,this.beads,this.inst,r),[this.inst,r+i]}};var er=class{constructor(){this.events=null;this.cursor=0;this.lastT=-1/0;this.hits=[]}step(e,t){let{hits:r}=this;if(r.length=0,!e)return this.events=null,r;let i=e!==this.events||t<this.lastT||t-this.lastT>.5;if(this.lastT=t,i){this.events=e;let n=0,a=e.length;for(;n<a;){let s=n+a>>1;e[s].t<=t?n=s+1:a=s}return this.cursor=n,r}for(;this.cursor<e.length&&e[this.cursor].t<=t;this.cursor+=1){let n=e[this.cursor];n.rhythmic!==!1&&r.push({index:this.cursor,gain:Math.max(.35,n.impact??1),down:n.kind==="downbeat"})}return r}};var Oi=["write","fade","breath"],Wi=["fog","fingers"],zr=["ribbon","lines2","lines3","cross","pillar","header","badge","columns","orbit"],Vr=["spot","buildings","lights","cars","road"],Xr=["slant","straight","off"],Kr=["night","aquarium","backdrop"],jr=["rain","dew","snow"],qi={night:"rain",aquarium:"dew"},Be=24,tr=12,_e={jitter:{min:0,max:1.5},warp:{min:0,max:1.5},mist:{min:.2,max:1},hold:{min:0,max:6},refog:{min:.5,max:12},strokeWidth:{min:.04,max:.16},writeSpeed:{min:.4,max:2.5},doodle:{min:0,max:1},busSpeed:{min:0,max:3},neon:{min:0,max:2},spot:{min:0,max:2},spotX:{min:0,max:1},rainHit:{min:0,max:3},rainBig:{min:0,max:2},bump:{min:0,max:2.5},jellies:{min:0,max:Be},jellySize:{min:.5,max:1.8},bubbles:{min:0,max:1},dew:{min:0,max:1.5},dewSize:{min:.5,max:2},dewGlint:{min:0,max:2},snow:{min:0,max:3},snowSize:{min:.5,max:2},snowMelt:{min:.25,max:3}},ot={render:{scale:"global",antialias:"global"},appear:["write","fade"],fade:["fog"],layouts:[...zr],behind:"night",glass:"rain",scenery:[...Vr],rain:"slant",credits:!0,jitter:.6,warp:.6,mist:.75,hold:1.2,refog:4,strokeWidth:.09,writeSpeed:1,doodle:.3,busSpeed:1,neon:1,spot:.5,spotX:.8,rainHit:1,rainBig:1,bump:1,jellies:16,jellySize:1,bubbles:.6,dew:1,dewSize:1,dewGlint:1,snow:1,snowSize:1,snowMelt:1};var Ua=[[1,.35,.72],[.35,.8,1],[.72,.5,1],[1,.72,.35]],Yr=-.35,Hi=1.35,zi=.4,Oa=.6,Ie=o=>{let e=Math.sin(o*127.1+311.7)*43758.5453;return e-Math.floor(e)},Vi=o=>o.kickAge<.07?o.kickFrom+(o.kick-o.kickFrom)*o.kickAge/.07:o.kick*Math.exp(-(o.kickAge-.07)/.3),Wa=1.5,Xi=(o,e,t)=>({xf:Ie(e*3.1+o),y:t,depth:Ie(e*5.7+o*1.3),rate:1.6+.9*Ie(e*2.3+o),phase:Ie(e+o*7.3)*Math.PI*2,rise:.012+.012*Ie(e*9.1+o),hue:Ie(e*4.4+o*11),kick:0,kickFrom:0,kickAge:10,seed:e}),rr=class{constructor(){this.list=Array.from({length:Be},(e,t)=>Xi(t,t+1,Yr+Ie(t*13.7)*(Hi-Yr)));this.palette=[[1,.81,.54],[.54,.8,1],[.8,.54,1]];this.own=1}setPalette(e,t,r){this.palette=[e,t,r]}step(e,t,r,i,n,a,s){let l=Math.round(n.jellies),c=s>0?zi:1;this.own+=Math.sign(c-this.own)*Math.min(Math.abs(c-this.own),t*(1-zi)/Wa);let{own:u}=this,{jelly:h,jellyColor:m}=e;for(let f=0;f<l;f+=1){let b=this.list[f];b.phase+=t*b.rate;let p=Math.max(Math.sin(b.phase),0);for(let v of a){if(s<=0)break;b.kickFrom=Vi(b),b.kick=Math.max(b.kickFrom,v.gain*s*(v.down?1:Oa)),b.kickAge=0}b.kickAge+=t;let y=Vi(b);b.y-=t*(b.rise*(1+.9*p*u)+.06*y),b.y<Yr&&(b=this.list[f]=Xi(f,b.seed+Be,Hi));let g=(b.xf+.02*Math.sin(r*.15+b.hue*10))*(i+.2)-.1,d=f*4;h[d]=g,h[d+1]=b.y,h[d+2]=(.03+.055*b.depth)*n.jellySize,h[d+3]=Math.min(1.3,Math.max(p**3*u,y));let w=Ua[Math.floor(b.hue*4)%4],x=this.palette[Math.floor(b.hue*3.7%1*3)],E=.45+.55*b.depth;for(let v=0;v<3;v+=1)m[d+v]=(w[v]*.65+x[v]*.35)*E;m[d+3]=b.phase}e.count=l}};var or=2*Math.PI*2,qa=2*.3*or,Ha=or*1.25,nr=1/120,Ki=6,ir=Ki*3+1,ar=class{constructor(){this.x=0;this.v=0;this.side=1;this.carry=0;this.history=new Float32Array(ir);this.head=0}kick(e){this.side=-this.side,this.v=this.side*e*Ha}step(e,t){for(this.carry+=Math.min(e,.1);this.carry>=nr;)this.carry-=nr,this.v+=(-or*or*this.x-qa*this.v)*nr,this.x+=this.v*nr,this.head=(this.head+1)%ir,this.history[this.head]=this.x;for(let r=0;r<4;r+=1)t[r]=this.history[(this.head-r*Ki+ir*2)%ir]}};var za=6,Va=.45,$r=.06,Xa=.25,ji=o=>{let e=Math.sin(o*91.7+47.3)*43758.5453;return e-Math.floor(e)},sr=class{constructor(){this.jellies=new rr;this.cursor=new er;this.out={jelly:new Float32Array(Be*4),jellyColor:new Float32Array(Be*4),count:0,bubbles:0,sway:new Float32Array(4),weedIdle:1,bursts:new Float32Array(tr*4),burstCount:0,beatRows:0,spotScale:1};this.spotKick=0;this.spotFrom=0;this.spotAge=10;this.weed=new ar;this.nextBurst=0}setPalette(e,t,r){this.jellies.setPalette(e,t,r)}step(e,t,r,i,n){let{out:a}=this,s=this.cursor.step(n?.events??null,n?.heard??0);this.jellies.step(a,e,t,r,i,s,n?.jelly??0),a.bubbles=i.bubbles;let{sway:l,bursts:c}=a;for(let m of s){let f=n?.weed??0,b=n?.bubble??0;f>0&&this.weed.kick(m.gain*f*(m.down?1:.7));let p=n?.spot??0;if(p>0&&(this.spotFrom=this.spotLevel(),this.spotKick=Math.max(this.spotFrom,Math.min(1.5,m.gain*p*(m.down?1:.7))),this.spotAge=0),b>0){let y=this.nextBurst*4;c[y]=.17+ji(m.index)*Math.max(0,r-.34),c[y+1]=t,c[y+2]=Math.min(1,m.gain*b*(m.down?1:.75)),c[y+3]=ji(m.index*3.3+1)*100,this.nextBurst=(this.nextBurst+1)%tr}}a.beatRows=n&&n.bubble>0?1:0,this.spotAge+=e,a.spotScale=1+Va*this.spotLevel(),this.weed.step(e,l);let u=n&&n.weed>0?0:1;a.weedIdle+=(u-a.weedIdle)*(1-Math.exp(-e/.6));let h=0;for(let m=0;m<tr;m+=1){let f=t-c[m*4+1];c[m*4+2]>0&&f>=0&&f<za?h=m+1:c[m*4+2]=0}return a.burstCount=h,a}spotLevel(){let e=this.spotAge;return e<$r?this.spotFrom+(this.spotKick-this.spotFrom)*e/$r:this.spotKick*Math.exp(-(e-$r)/Xa)}};var at=$e.nebbiaAquaBeat,Ka=xe(at,"jelly"),ja=xe(at,"weed"),Ya=xe(at,"bubble"),$a=xe(at,"spot"),Ja=["appear","fade","layouts","credits","jitter","warp","hold","refog","writeSpeed","doodle"],Qa=(o,e)=>Array.isArray(o)&&Array.isArray(e)?o.join()===e.join():o===e,lr=class{constructor(e,t,r,i=1){this.input=e;this.canvas=document.createElement("canvas");this.mask=new zt;this.motion=new dt;this.spot=new Xt;this.energy=new Ct;this.fieldInst=[];this.snow=null;this.observer=null;this.hits=new He(0);this.laid=[];this.words=[];this.vocalEnv=null;this.drizzleRide=new jt;this.clock=new Yt;this.aquaBeatsOut={events:[],heard:0,jelly:0,weed:0,bubble:0,spot:0};this.song={lines:[],seed:"nebbia"};this.fontScale=1;this.showText=!0;this.paused=!1;this.still=!1;this.W=0;this.H=0;this.raf=0;this.buildToken=0;this.firstDrawn=!1;this.dead=!1;this.fontReady=!0;this.startedAt=performance.now();this.shadersLogged=!1;this.paperFrame=()=>{this.paused&&this.requestFrame()};this.handleContextLost=e=>{e.preventDefault(),cancelAnimationFrame(this.raf),this.dead=!0,this.input.onFailed()};this.handleContextRestored=()=>{this.input.onRestored()};this.handleFontsLoaded=()=>{if(this.dead)return;let e=this.glyphs.fontReady(),t=e&&!this.fontReady;this.fontReady=e,t&&(this.glyphs.clear(),this.rebuild())};this.frame=()=>{if(this.raf=0,this.dead||this.gl.isContextLost||!this.shadersReady()||!this.W||!this.H)return;let e=performance.now()/1e3,t=this.paused||this.still,r=this.clock.tick(e,this.paused),i=this.input.currentTime.get(),n=this.tuning,a=this.drizzleRide,s=this.input.glass==="rain";s||!this.aquarium?a.step(this.motion,e,i,t,n.bump,!this.aquarium):this.motion.beginFrame(e,t,!1);let l=s?a.rain:0;s&&a.env!==this.vocalEnv&&(this.vocalEnv=a.env,this.rebuildHits());let c=this.energy.update(this.input.vocal.get(),Math.max(r,1/60));this.hits.advance(i,c,this.still?0:n.rainHit*l,He.lifeOf(n.refog,_.beadHold),this.W,this.H),this.mask.draw(this.laid,i,e,n,this.motion,this.hits,n.rainBig*l),this.spot.update(this.laid,i,r);let u=this.aquarium?.step(r,this.clock.time,this.W/this.H,n,this.aquaBeats())??null;this.snow?(this.snow.step(t?0:r,n,this.mask.sweeps??[],this.mask.wet??[],this.aquarium?0:n.busSpeed,!this.aquarium),this.snow.drawFog(this.mask.ctx,n)):this.condensation&&this.condensation.touchInk(this.mask.sweeps??[],this.mask.wet??[],n.dewSize);let h=this.condensation?this.condensation.instances(t?0:r,n.dew,n.dewSize):this.snow?this.snow.instances(this.fieldInst):this.motion.fieldInstances(this.fieldInst,n.busSpeed),m=ki(n,this.motion.shake(),this.spot,this.W,this.H,this.clock.time,u);this.gl.render(this.mask.ctx,this.mask.breath.active?this.mask.breath:null,this.motion.drops,h,m,this.condensation?.changed??this.snow?.changed??!0),this.snow&&this.gl.drawFlakes(...this.snow.flakeInstances(this.mask.ice??[]),m.glassShift),this.fireflies&&this.gl.drawFireflies(...this.fireflies.step(t?0:r)),this.firstDrawn||(this.firstDrawn=!0,this.input.onFirstFrame()),this.paused||(this.raf=requestAnimationFrame(this.frame))};this.theme=t,this.tuning=r,this.renderScale=i,this.aquarium=e.scene==="aquarium"?new sr:null,this.condensation=e.glass==="dew"?new $t:null,this.fireflies=e.glass==="dew"?new Ft:null,e.glass!=="rain"&&(this.mask.sweeps=[],this.mask.wet=[]),e.glass==="snow"&&(this.mask.ice=[]),this.motion.bump=r.bump,this.canvas.style.cssText="position:absolute;inset:0;width:100%;height:100%;display:block",this.glyphs=new It(Ye(t),je(t,600)),this.canvas.setAttribute("data-lumenic-liquid-glass-scene-effect","true"),e.host.appendChild(this.canvas);try{this.gl=new Bt(this.canvas,e.seeThrough,e.scene,e.glass),this.gl.onSpriteReady=()=>this.requestFrame(),parent.document.querySelector("canvas[data-music-background]")?.addEventListener("lume-glass-media-frame",this.paperFrame),e.glass==="snow"&&(this.snow=new Zt,this.gl.setFlakeAtlas(...this.snow.atlas())),this.canvas.addEventListener("webglcontextlost",this.handleContextLost),this.canvas.addEventListener("webglcontextrestored",this.handleContextRestored),this.observer=new ResizeObserver(()=>this.resize()),this.observer.observe(e.host),this.applyTheme(),this.resize()}catch(n){throw this.dead=!0,this.observer?.disconnect(),this.canvas.removeEventListener("webglcontextlost",this.handleContextLost),this.canvas.removeEventListener("webglcontextrestored",this.handleContextRestored),this.canvas.getContext("webgl2")?.getExtension("WEBGL_lose_context")?.loseContext(),this.canvas.remove(),n}this.fontReady=this.glyphs.fontReady(),document.fonts?.addEventListener("loadingdone",this.handleFontsLoaded),si().then(n=>{n&&!this.dead&&(this.glyphs.clear(),this.rebuild())})}setSong(e){e.seed!==this.song.seed&&this.clock.songChanged(),this.song=e,this.rebuild()}setFontScale(e){e!==this.fontScale&&(this.fontScale=e,this.rebuild())}setShowText(e){e!==this.showText&&(this.showText=e,this.rebuild())}setDaylight(e){this.gl.mistTint=e?[.86,.89,.92]:[.07,.08,.1],this.requestFrame()}setTheme(e){let t=String(ut(e.lyricsIcons))!==String(ut(this.theme.lyricsIcons));this.theme=e;let r=this.glyphs.setFont(Ye(e),je(e,600));r&&(this.fontReady=this.glyphs.fontReady()),this.applyTheme(),r||t?this.rebuild():this.requestFrame()}setTuning(e){let t=this.tuning;this.tuning=e,this.motion.bump=e.bump,Ja.some(r=>!Qa(t[r],e[r]))&&this.rebuild(),this.requestFrame()}setRenderScale(e){e!==this.renderScale&&(this.renderScale=e,this.resize())}setPlayback(e,t){this.paused=e,this.still=t,this.requestFrame()}requestFrame(){!this.raf&&!this.dead&&(this.raf=requestAnimationFrame(this.frame))}destroy(){parent.document.querySelector("canvas[data-music-background]")?.removeEventListener("lume-glass-media-frame",this.paperFrame),this.dead=!0,cancelAnimationFrame(this.raf),this.observer?.disconnect(),document.fonts?.removeEventListener("loadingdone",this.handleFontsLoaded),this.canvas.removeEventListener("webglcontextlost",this.handleContextLost),this.canvas.removeEventListener("webglcontextrestored",this.handleContextRestored),this.gl.destroy(),this.canvas.remove()}applyTheme(){let e=Fi(this.theme);this.gl.setPalette(e.sky0,e.sky1,e.la,e.lb,e.lc),this.aquarium?.setPalette(e.la,e.lb,e.lc),this.fireflies?.setPalette(e.la,e.lb,e.lc)}aquaBeats(){if(this.still||!xn()||!ht(at))return null;let e=mt().time;if(e===null)return null;let t=this.aquaBeatsOut;return t.events=yn(),t.heard=e,t.jelly=ye(Ka),t.weed=ye(ja),t.bubble=ye(Ya),t.spot=ye($a),t}resize(){let{clientWidth:e,clientHeight:t}=this.input.host;if(e<1||t<1)return;let r=e!==this.W||t!==this.H;this.W=e,this.H=t;let i=hn(Math.min(2,window.devicePixelRatio||1),this.renderScale);this.gl.resize(this.W,this.H,i),this.mask.resize(this.W,this.H),this.snow?.resize(this.W,this.H),this.fireflies?.resize(this.W,this.H),r&&(this.condensation?this.condensation.build(this.W,this.H):this.snow||this.motion.buildField(this.W,this.H),this.rebuild()),this.requestFrame()}rebuild(){let e=++this.buildToken,{lines:t,seed:r,title:i,artist:n}=this.song,a=ut(this.theme.lyricsIcons),s=a.length?a:En;fn([...s,...An(t)]).catch(()=>{}).then(()=>{if(e!==this.buildToken||this.dead||!this.W)return;let l=this.showText?Tn(Sn(t,r,this.tuning.doodle,s),this.tuning.credits?i:null,n):[];this.laid=pi(l,this.glyphs,this.tuning,{W:this.W,H:this.H,fontScale:this.fontScale},r),this.hits=new He(de(String(r))),this.words=l.map(c=>c.words),this.rebuildHits(),this.spot.reset(),this.requestFrame()})}rebuildHits(){this.hits.buildBig(this.laid,this.words,this.W,this.H,this.vocalEnv)}shadersReady(){try{if(this.gl.ready)return this.shadersLogged||(this.shadersLogged=!0,console.info(`[Nebbia] shaders ready ${Math.round(performance.now()-this.startedAt)} ms after start`)),!0}catch(e){return console.warn("[Nebbia] shaders failed to build",e),this.dead=!0,this.input.onFailed(),!1}return this.raf=requestAnimationFrame(this.frame),!1}};var Jr=(o,e,t)=>{let r=Array.isArray(e)?o.filter(i=>e.includes(i)):[];return r.length?r:[...t]},Za={auto:["write","fade"],write:["write"],fade:["fade"]},es={fog:["fog"],fingers:["fingers"],mix:["fog","fingers"]},$i=(o,e)=>{if(o!==void 0)return mr(o);let t=mr(null);if(e===void 0)return t;let r=pr(e,1,.5,2);if(r===1)return t;let i=hr.reduce((n,a)=>Math.abs(a-r)<=Math.abs(n-r)?a:n,hr[0]);return{...t,scale:i}},Yi=o=>{let e=o&&typeof o=="object"?o:{},t=ot,r=Object.fromEntries(Object.keys(_e).map(s=>[s,pr(e[s],t[s],_e[s].min,_e[s].max)])),i=e.appear??Za[String(e.appearMode)],n=e.fade??es[String(e.fadeMode)],a=Kr.find(s=>s===e.behind)??t.behind;return{render:$i(e.render,e.resolution),appear:Jr(Oi,i,t.appear),fade:Jr(Wi,n,t.fade),layouts:Jr(zr,e.layouts,t.layouts),behind:a,glass:jr.find(s=>s===e.glass)??qi[a]??t.glass,scenery:Array.isArray(e.scenery)?Vr.filter(s=>e.scenery.includes(s)):[...t.scenery],rain:Xr.find(s=>s===e.rain)??t.rain,credits:typeof e.credits=="boolean"?e.credits:t.credits,...r}},ts=vn({mode:"nebbia",defaults:ot,modKeys:{behind:ft(Kr),glass:ft(jr),rain:ft(Xr),credits:gn,...Object.fromEntries(Object.entries(_e).map(([o,{min:e,max:t}])=>[o,br(e,t)])),jellies:br(_e.jellies.min,_e.jellies.max,1)},settingsKey:"nebbiaTuning",settingsSetterKey:"handleSetNebbiaTuning",settingsResetterKey:"handleResetNebbiaTuning",storage:{key:"nebbia_tuning",read:Yi,merge:(o,e)=>{let t=e.resolution,r=e.render===void 0&&t!==void 0?{...e,render:$i(void 0,t)}:e;return Yi({...o,...r})},resetNotice:"notifications.nebbiaReset"}});var Ji=(o,e,t=!1)=>o.behind==="backdrop"||!t&&!!e?.transparent;var Ne=ur(en(),1),rs=[],ns=o=>{let{currentTime:e,currentLineIndex:t,lines:r,theme:i,audioPower:n,audioBands:a,showText:s=!0,lyricsFontScale:l=1,staticMode:c=!1,paused:u=!1,seed:h="nebbia",songTitle:m,songArtist:f,isPlayerChromeHidden:b=!1,hideTranslationSubtitle:p=!1,isPreviewMode:y=!1,subtitleOverlayLayout:g,subtitleTheme:d,subtitleFontScale:w,subtitleOverlayOpacity:x,subtitleOverlayBackground:E,subtitleUpcomingLyricsBlur:v,isDaylight:A=!1,nebbiaTuning:M=ot}=o,{t:T}=tn(),F=(0,H.useRef)(null),R=(0,H.useRef)(null),[L,P]=(0,H.useState)(!1),[D,U]=(0,H.useState)(0),[j,z]=(0,H.useState)(!1),Z=sn(),se=mn(M.render),V=y?1:se.scale,ie=Ji(M,o.background,o.isPreviewMode),ne=M.behind==="aquarium"?"aquarium":"night",k=M.glass,C=an({seed:h,lines:r,currentTime:e,readyGraceMs:3e3}),O=C.isInstrumental?rs:C.lines,B=(0,H.useMemo)(()=>({lines:O,seed:C.seed??"nebbia",title:m,artist:f}),[O,C.seed,m,f]),G=(0,H.useRef)({theme:i,tuning:M,renderScale:V,song:B,lyricsFontScale:l,showText:s,isDaylight:A,paused:u,staticMode:c,firstFrameDrawn:Z});G.current={theme:i,tuning:M,renderScale:V,song:B,lyricsFontScale:l,showText:s,isDaylight:A,paused:u,staticMode:c,firstFrameDrawn:Z},(0,H.useEffect)(()=>{let oe=F.current;if(!oe)return;let ee;try{let K=G.current;ee=new lr({host:oe,seeThrough:ie,scene:ne,glass:k,currentTime:e,vocal:a.vocal,onFirstFrame:()=>{z(!0),G.current.firstFrameDrawn()},onFailed:()=>{P(!0),z(!0),G.current.firstFrameDrawn()},onRestored:()=>U(cr=>cr+1)},K.theme,K.tuning,K.renderScale),ee.setFontScale(K.lyricsFontScale),ee.setShowText(K.showText),ee.setDaylight(K.isDaylight),ee.setPlayback(K.paused,K.staticMode),ee.setSong(K.song)}catch(K){console.warn("[Nebbia] runtime failed to start",K),P(!0),z(!0),G.current.firstFrameDrawn();return}return R.current=ee,P(!1),()=>{R.current=null,ee.destroy()}},[a.vocal,e,D,k,ne,ie]),(0,H.useEffect)(()=>{R.current?.setSong(B)},[B]),(0,H.useEffect)(()=>{R.current?.setTuning(M)},[M]),(0,H.useEffect)(()=>{R.current?.setRenderScale(V)},[V]),(0,H.useEffect)(()=>{R.current?.setTheme(i)},[i]),(0,H.useEffect)(()=>{R.current?.setFontScale(l)},[l]),(0,H.useEffect)(()=>{R.current?.setShowText(s)},[s]),(0,H.useEffect)(()=>{R.current?.setDaylight(A)},[A]),(0,H.useEffect)(()=>{R.current?.setPlayback(u,c)},[u,c]),(0,H.useEffect)(()=>e.on("change",()=>{u&&R.current?.requestFrame()}),[e,u]);let{activeLine:Y,recentCompletedLine:Pe,nextLines:De}=on({currentTime:e,currentLineIndex:t,lines:r,getLineEndTime:ct}),le=ln(l);return(0,Ne.jsxs)(rn,{theme:i,audioPower:n,audioBands:a,sharedProps:o,renderBackground:ie,children:[(0,Ne.jsxs)("div",{className:"pointer-events-none absolute inset-0 z-10 overflow-hidden",style:{background:ie?void 0:"#05070a"},children:[(0,Ne.jsx)("div",{ref:F,className:"absolute inset-0","aria-hidden":"true",...j?{}:{[wn]:""}}),L&&(0,Ne.jsx)("div",{className:"absolute inset-0 flex items-center justify-center px-10 text-center",style:{color:i.primaryColor,fontFamily:Ye(i),fontWeight:je(i,600),fontSize:`clamp(2rem, ${5.4*l}vw, 5.6rem)`},children:s&&!C.isInstrumental?Y?.fullText||T("ui.waitingForMusic"):null})]}),(0,Ne.jsx)(un,{showText:s,activeLine:Y,recentCompletedLine:Pe,nextLines:De,theme:i,subtitleTheme:d,translationFontSize:le.translationFontSize,upcomingFontSize:le.upcomingFontSize,subtitleFontScale:w,subtitleOverlayOpacity:x,subtitleOverlayBackground:E,subtitleUpcomingLyricsBlur:v,isPlayerChromeHidden:b,hideTranslationSubtitle:p,subtitleOverlayLayout:g})]})},cu=ns;export{cu as default};
