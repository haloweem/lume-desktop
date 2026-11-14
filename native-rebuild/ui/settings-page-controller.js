// A bounded registry for component settings pages. Navigation, preview leases,
// timers and host protocol validation remain owned by their existing callers.
export function createSettingsPageController({root,pages,showNotice,afterSync=()=>{}}) {
  const byScope=new Map(),byMessage=new Map();
  for(const page of pages) {
    if(byScope.has(page.scope))throw new Error(`Duplicate settings scope: ${page.scope}`);
    byScope.set(page.scope,page);
    for(const message of page.messages||[]) {
      if(byMessage.has(message))throw new Error(`Duplicate settings message: ${message}`);
      byMessage.set(message,page.scope);
    }
  }
  const syncScopes=(scopes,state)=>{
    for(const scope of scopes)byScope.get(scope)?.sync?.(root,state);
  };
  const statusKey=scope=>scope==='clock'?'status':`${scope}Status`;
  const errorKey=scope=>scope==='clock'?'error':`${scope}Error`;
  return {
    render(scope,state,tab) {return byScope.get(scope)?.render?.(state,tab);},
    syncScopes,
    receive(event,previous,state) {
      let scope;
      if(event.type==='request'||event.type==='unavailable')scope=event.scope||'clock';
      else if(event.type==='host') {
        scope=byMessage.get(event.message.type);
        if(scope===undefined)return false;
      } else return false;
      syncScopes(scope==='all'?[...byScope.keys()]:[scope],state);
      afterSync(scope,state);
      const selected=byScope.get(scope);
      const page=selected?.notice?selected:byScope.get('clock');
      const feedback=page?.notice?.(state,{
        previousStatus:previous[statusKey(page.scope)],
        hasInlineStatus:!!root.querySelector(`[data-settings-scope="${page.scope}"] .status`),
      });
      if(feedback) {
        const status=state[statusKey(scope)],error=state[errorKey(scope)];
        const success=!error&&(status==='saved'||(scope==='taskbar'&&status==='applied'&&
          !state.taskbar.pendingRecovery&&state.taskbar.enabled===state.taskbar.running));
        showNotice(feedback,{autoDismiss:success});
      }
      return true;
    },
  };
}
