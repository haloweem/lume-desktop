import test from 'node:test';
import assert from 'node:assert/strict';
import {initialState,reduce} from '../state.js';
import {modules,localizedItem} from '../catalog.js';
import {setLanguage} from '../localization.js';

test('桌面图标默认只允许双击切换，不自动隐藏',()=>{
  const {icons}=initialState();
  assert.equal(icons.enabled,false);
  assert.equal(icons.enableAutoHide,false);
  assert.equal(icons.autoHideDelay,5);
  assert.equal(icons.showIconsOnAnyInput,true);
});

test('已保存的自动隐藏选择不会被新默认值覆盖',()=>{
  for(const enableAutoHide of [true,false]) {
    const message={version:1,type:'icons-state',...initialState().icons,enableAutoHide,
      loadStatus:'loaded',error:'',result:'snapshot',saved:false};
    const state=reduce(initialState(),{type:'host',message});
    assert.equal(state.icons.enableAutoHide,enableAutoHide);
  }
});

test('图标封面说明突出双击桌面隐藏图标，中英文一致',()=>{
  const icons=modules.find(item=>item.id==='icons');
  try {
    setLanguage('zh-CN');
    assert.equal(localizedItem(icons).description,'双击桌面隐藏图标');
    setLanguage('en');
    assert.equal(localizedItem(icons).description,'Double-click the desktop to hide icons');
  } finally {setLanguage('zh-CN');}
});
