// Warm only a bounded set of cover images. No timers or hidden-page work.
export function createWallpaperPreloader({createImage=()=>new Image(),onSuccess=()=>{},onFailure=()=>{},limit=24,concurrency=2}={}){
  const records=new Map();let visible=true,active=0;
  function drain(){
    if(!visible)return;
    for(const record of records.values()){
      if(active>=concurrency)break;
      if(record.status!=='queued')continue;
      const image=createImage();record.image=image;record.status='loading';++active;
      image.decoding='async';image.fetchPriority='low';
      const finish=ok=>{
        if(record.status!=='loading')return;
        record.status=ok?'ready':'failed';--active;
        image.onload=image.onerror=null;record.image=null;
        (ok?onSuccess:onFailure)(record.target.source,image.currentSrc||image.src);
        drain();
      };
      image.onload=()=>finish(true);image.onerror=()=>finish(false);image.src=record.target.url;
    }
  }
  return {
    warm(targets){
      if(!visible)return;
      for(const target of targets){
        const key=`${target.source}\n${target.url}`;
        if(records.has(key))continue;
        while(records.size>=limit){
          const oldest=[...records].find(([,record])=>['ready','failed'].includes(record.status));
          if(!oldest)break;records.delete(oldest[0]);
        }
        if(records.size>=limit)break;
        records.set(key,{target,status:'queued',image:null});
      }
      drain();
    },
    setVisible(value){
      if(visible===value)return;visible=value;
      if(!visible)for(const [key,record] of records){
        if(record.status==='ready')continue;
        record.status='cancelled';
        if(record.image){record.image.onload=record.image.onerror=null;record.image.removeAttribute('src');record.image=null;--active;}
        records.delete(key);
      }
    },
    dispose(){this.setVisible(false);records.clear();},
  };
}
