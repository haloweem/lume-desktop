import { t } from "./localization.js";

export const modules = [
  {
    id: "taskbar",
    title: "任务栏透明化",
    description: "任务栏透明",
    toggle: true,
  },
  {
    id: "icons",
    title: "桌面图标切换与自动隐藏",
    description: "双击桌面隐藏图标",
    toggle: true,
  },
  {
    id: "minimize",
    title: "macOS 最小化动画",
    description: "窗口流畅缩小",
    toggle: true,
    hidden: true, // Temporarily keep the feature and preferences, but omit its entry.
  },
  {
    id: "dock",
    title: "Dock栏美化",
    description: "启动台与常用应用",
    toggle: true,
  },
  {
    id: "dynamicIsland",
    title: "灵动岛",
    description: "音乐播放信息与控制",
    toggle: true,
  },
  {
    id: "radial",
    title: "轮盘菜单",
    description: "快速使用截图等桌面工具",
    toggle: true,
    hidden: true, // Temporarily keep the feature and preferences, but omit its entry.
  },
  {
    id: "widgets",
    title: "桌面组件",
    description: "时间、日历、天气、黄历、性能、音乐、音频频谱与图标收纳",
  },
];
export const widgets = [
  {
    id: "clock",
    title: "时间",
    description:
      "在桌面显示时间，提供样式一、样式二、样式三三种样式。",
  },
  {
    id: "calendar",
    title: "日历",
    description: "在桌面显示月视图日历，并高亮今日日期。",
  },
  {
    id: "weather",
    title: "天气",
    description: "在桌面显示当前天气，支持自动定位或选择城市。",
  },
  {id:"todayTodo",title:"待办清单",description:"在桌面管理今日待办，支持勾选、编辑和删除。"},
  { id: "almanac", title: "黄历", description: "在桌面显示农历与每日宜忌。" },
  {
    id: "performance",
    title: "性能",
    description: "在桌面查看 CPU、GPU、内存与网络状态。",
  },
  {
    id: "music",
    title: "音乐",
    description: "在桌面显示 Lume 音乐的歌曲和进度，并控制播放、暂停与切歌。",
  },
  {
    id: "spectrum",
    title: "音频频谱",
    description: "跟随电脑正在播放的声音，在桌面显示彩色音频频谱。",
  },
  {
    id: "notes",
    title: "便签",
    description: "双击桌面便签直接编辑，在设置中统一管理笔记。",
    hidden: true,
  },
  {
    id: "organizer",
    title: "图标收纳",
    description: "在桌面集中收纳快捷方式、文件与文件夹。",
  },
];

const englishItems = {
  todayTodo:["Today’s tasks","Manage today’s tasks on your desktop: complete, edit or delete them."],
  taskbar: ["Taskbar transparency", "Transparent taskbar"],
  icons: ["Desktop icons", "Double-click the desktop to hide icons"],
  minimize: ["macOS minimize animation", "Smooth window minimizing"],
  dock: ["Dock", "Launchpad and favourite apps"],
  dynamicIsland: ["Dynamic Island", "Music playback information and controls"],
  radial: ["Radial menu", "Quick access to screenshot and desktop tools"],
  widgets: ["Desktop widgets", "Clock, calendar, weather, almanac, performance, music, audio spectrum and icon organizer"],
  notes: ["Notes", "Show a sticky note on your desktop; open it to write and manage Markdown notes."],
  clock: ["Clock", "Display the time on your desktop in three styles: Style 1, Style 2 and Style 3."],
  calendar: ["Calendar", "Display a monthly calendar on your desktop with today highlighted."],
  weather: ["Weather", "Display current weather on your desktop using automatic location or a selected city."],
  almanac: ["Almanac", "Display the lunar calendar and daily auspicious and inauspicious activities."],
  performance: ["Performance", "View CPU, GPU, memory and network status on your desktop."],
  music: ["Music", "Display the current Lume Music track and progress, with playback and track controls."],
  spectrum: ["Audio spectrum", "Show a colourful audio spectrum of whatever your computer is playing."],
  organizer: ["Icon organizer", "Organize shortcuts, files and folders on your desktop."],
};

// Translate only built-in catalog entries. Search and host/user records stay untouched.
export function localizedItem(item) {
  const original = [...modules, ...widgets].find(entry => entry.id === item.id);
  const english = englishItems[item.id];
  if (!original || !english || item.title !== original.title || item.description !== original.description) return item;
  return { ...item, title: t(item.title, english[0]), description: t(item.description, english[1]) };
}
