import {clockPage,syncClockControls} from './clock.js';
import {calendarPage,calendarNotice,syncCalendarControls} from './calendar.js';
import {performancePage,performanceNotice,syncPerformanceControls} from './performance.js';
import {spectrumPage,spectrumNotice,syncSpectrumControls} from './spectrum.js';
import {todayTodoPage,todayTodoNotice,syncTodayTodoControls} from './today-todo.js';
import {almanacPage,almanacNotice,syncAlmanacControls} from './almanac.js';
import {weatherPage,weatherNotice,syncWeatherControls} from './weather.js';
import {organizerPage,organizerNotice,syncOrganizerControls} from './organizer.js';
import {desktopIconsPage,desktopIconsNotice,syncDesktopIconsControls} from './desktop-icons.js';
import {taskbarPage,taskbarNotice,syncTaskbarControls} from './taskbar.js';
import {minimizePage,minimizeNotice,syncMinimizeControls} from './minimize.js';
import {dockPage,dockNotice,syncDockControls} from './dock.js';
import {islandPage,syncIslandControls} from './dynamic-island.js';
import {radialPage,radialNotice,syncRadialControls} from './radial.js';
import {settingsNotice} from './state.js';
import {t} from './localization.js';
import {createSettingsPageController} from './settings-page-controller.js';

function clockNotice(state,options) {
  // Keep timing in settingsNotice and translate only its fixed copy.
  const feedback=settingsNotice(state.status,{...options,error:state.error});
  return {
    '桌面时钟启动失败，请检查字体和安装文件':t('桌面时钟启动失败，请检查字体和安装文件','Could not start the desktop clock. Check the fonts and installation files.'),
    '时钟恢复失败，请重新启用组件':t('时钟恢复失败，请重新启用组件','Could not restore the clock. Enable the widget again.'),
    '保存失败，已恢复此前设置，请重试':t('保存失败，已恢复此前设置，请重试','Could not save. Previous settings restored. Please try again.'),
    '宿主未连接，设置未保存':t('宿主未连接，设置未保存','Host disconnected. Settings were not saved.'),
    '已应用并保存':t('已应用并保存','Applied and saved'),
  }[feedback]||feedback;
}

// Each registration owns rendering, confirmed-state synchronization and its
// feedback policy. The order also preserves the existing all-scope refresh.
const pages=[
  {scope:'clock',messages:['settings'],render:clockPage,sync:syncClockControls,notice:clockNotice},
  {scope:'calendar',messages:['calendar-settings','reminder-status'],render:calendarPage,sync:syncCalendarControls,notice:calendarNotice},
  {scope:'performance',messages:['performance-state'],render:performancePage,sync:syncPerformanceControls,notice:performanceNotice},
  {scope:'spectrum',messages:['spectrum-state'],render:spectrumPage,sync:syncSpectrumControls,notice:spectrumNotice},
  {scope:'todayTodo',messages:['todayTodo-state'],render:todayTodoPage,sync:syncTodayTodoControls,notice:todayTodoNotice},
  {scope:'almanac',messages:['almanac-state'],render:almanacPage,sync:syncAlmanacControls,notice:almanacNotice},
  {scope:'weather',messages:['weather-state'],render:weatherPage,sync:syncWeatherControls,notice:weatherNotice},
  {scope:'organizer',messages:['organizer-state'],render:organizerPage,sync:syncOrganizerControls,notice:organizerNotice},
  {scope:'icons',messages:['icons-state'],render:desktopIconsPage,sync:syncDesktopIconsControls,notice:desktopIconsNotice},
  {scope:'taskbar',messages:['taskbar-state'],render:taskbarPage,sync:syncTaskbarControls,notice:taskbarNotice},
  {scope:'minimize',messages:['minimize-state'],render:minimizePage,sync:syncMinimizeControls,notice:minimizeNotice},
  {scope:'dock',messages:['dock-state'],render:dockPage,sync:syncDockControls,notice:dockNotice},
  // Island preview and feedback remain in their dedicated app controller.
  {scope:'dynamicIsland',render:islandPage,sync:syncIslandControls},
  {scope:'radial',messages:['radial-state'],render:radialPage,sync:syncRadialControls,notice:radialNotice},
];

export function createComponentSettingsController(options) {
  return createSettingsPageController({...options,pages});
}
