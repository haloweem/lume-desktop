// Display identity only; provider authorization and media resolution stay native.
export function musicCollectionKey(item) {
  if(!item || !['string','number'].includes(typeof item.id))return '';
  const id=String(item.id);
  if(!id || id.length>256 || (typeof item.id==='number' && !Number.isSafeInteger(item.id)))return '';
  const provider=typeof item.providerId==='string'?item.providerId:'';
  const type=typeof item.type==='string'?item.type:'playlist';
  if(provider.length>64 || type.length>32)return '';
  return JSON.stringify([provider,type,id]);
}
