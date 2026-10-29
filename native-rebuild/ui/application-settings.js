import {arrow} from './dom.js';
import {syncSettingsControlBusy} from './settings-frame.js';
import {mountAccount,LUME_LOGO,REFRESH_ICON} from './account.js';
import {validApplicationSettingsState} from './settings-account-protocol.js';
import {DIAGNOSTICS_ENABLED} from './diagnostics.js';

export {validApplicationSettingsState as isApplicationSettingsSnapshot};

const sections=['general','user','autostart',...(DIAGNOSTICS_ENABLED?['diagnostics']:[]),'support','about'];
const text={
  'zh-CN':{
    settings:'设置',categories:'设置分类',back:'返回',general:'通用',user:'用户设置',autostart:'开机启动',diagnostics:'诊断与维护',support:'问题反馈',about:'关于',
    supportDescription:'遇到组件不显示、材质失效、任务栏异常、壁纸卡顿或程序崩溃时，生成一份诊断报告发给我们，我们可以据此直接定位原因。',
    supportPrivacy:'报告包含系统与显卡信息、各组件状态、运行日志和崩溃记录，不包含账号密码、登录凭据和位置信息。报告只保存在你的桌面，由你决定发给谁。',
    supportNote:'发生了什么（可选）',supportNotePlaceholder:'例如：开机后任务栏透明失效，重新打开 Lume 后恢复',
    supportCreate:'生成诊断报告',supportLogs:'打开日志文件夹',supportWorking:'正在生成诊断报告…',
    supportCopy:'复制诊断信息',supportCopying:'正在收集诊断信息…',supportCopied:'诊断信息已复制，可直接粘贴到评论区或聊天里。',
    supportChecks:'启动自检',supportChecking:'正在检查…',supportCheckOk:'正常',supportCheckProblem:'异常',supportCheckUnknown:'未知',supportFix:'建议：',
    supportDone:'已保存到桌面：{file}，请把这个文件发给我们。',supportFailed:'生成失败：{error}',supportUnavailable:'当前无法生成诊断报告。',
    language:'语言',languageDescription:'选择 Lume 的显示语言。',balancedMode:'均衡模式',
    balancedDescription:'动态壁纸以 30 fps 播放，全屏时自动暂停。',autostartLabel:'开机时自动启动 Lume',
    dataFolder:'用户数据目录',dataFolderDescription:'账号、待办、音乐库和设置保存在这里；更改后会在下次启动时搬移并生效。',
    wallpaperFolder:'壁纸下载目录',wallpaperFolderDescription:'壁纸文件保存在这里；更改后从下次启动开始，只影响之后下载的壁纸。',
    changeFolder:'更改…',
    autostartDescription:'登录 Windows 后自动启动 Lume。',version:'版本',checkUpdate:'检查更新',
    downloadUpdate:'下载并安装',installUpdate:'立即安装',loading:'正在读取设置...',saving:'正在保存...',
    checking:'正在检查更新...',current:'已是最新版本',available:'发现兼容的新版本',incompatible:'此更新与当前版本不兼容，不能直接安装。',
    downloading:'正在下载更新',ready:'更新已下载，可以安装',installing:'正在启动安装程序...',error:'检查或更新失败，请重试。',
    save:'保存失败，已恢复此前设置，请重试。',network:'网络连接失败，请稍后重试。',startupError:'开机启动设置保存失败，请重试。',
    unavailable:'宿主未连接，设置未保存。',unsupported:'此操作暂不可用。',failure:'操作失败，请重试。',
    readError:'设置读取失败，请重试。',balancedError:'壁纸播放策略应用失败，请重试。',balancedRollback:'壁纸播放策略恢复失败，请重试。',
    startupRollback:'开机启动设置恢复失败，请重试。',updateDisabled:'此版本暂不支持下载或安装更新。',
    startupConflict:'检测到其他安装目录或未知启动项，未作更改。',
    updateInvalid:'更新信息验证失败，不能安装。',updateTimeout:'检查更新超时，请重试。',updateCancelled:'已取消检查更新。',
    diagnosticDescription:'Lume 会自动记录各模块的错误状态；你只需在需要时补充现象，再复制或导出诊断摘要。',
    diagnosticPrivacy:'运行摘要自动保存在本机，复制或导出后由你决定是否分享，不会自动上传。窗口和运行状态不能证明每个像素都已正确显示。',
    diagnosticSummary:'问题描述（可选）',diagnosticSummaryPlaceholder:'如果愿意，可以补充你看到的现象',
    diagnosticCopy:'复制诊断摘要',diagnosticExport:'导出诊断包',
    diagnosticClear:'清除本地记录',diagnosticEvents:'已记录 {count} 条运行事件',diagnosticNative:'原生诊断：{status}',
    diagnosticMismatch:'检测到 {count} 个效果不一致',diagnosticFault:'检测到 {count} 个原生故障',
    diagnosticNativeReady:'正常记录中',diagnosticNativeMemory:'仅保存在当前进程',diagnosticNativeUnavailable:'暂不可用',diagnosticNativeFailed:'本地记录失败',
    diagnosticMismatchDetail:'{component}：预期 {expected}，当前 {actual}{reason}',
    diagnosticFaultDetail:'{component}：{code}{message}{count}',diagnosticReason:'（{reason}）',diagnosticFaultMessage:'（{message}）',diagnosticRepeat:'，重复 {count} 次',
    diagnosticCopied:'诊断摘要已复制，可直接发给 AI 或开发者。',
    diagnosticExported:'诊断包已导出。',diagnosticCleared:'本地诊断记录已清除。',diagnosticClearFailed:'原生记录暂不可清除，请稍后重试。',
    diagnosticClearConfirm:'确定清除问题记录和运行事件吗？',
  },
  en:{
    settings:'Settings',categories:'Settings categories',back:'Back',general:'General',user:'User settings',autostart:'Startup',diagnostics:'Diagnostics & maintenance',support:'Report a problem',about:'About',
    supportDescription:'If a component does not show, materials stop working, the taskbar misbehaves, the wallpaper stutters or Lume crashes, create a diagnostic report and send it to us so the cause can be found directly.',
    supportPrivacy:'The report contains system and graphics information, component states, runtime logs and crash records. It never includes passwords, sign-in credentials or your location. It is saved on your desktop and you decide who receives it.',
    supportNote:'What happened (optional)',supportNotePlaceholder:'For example: after startup the transparent taskbar stopped working until Lume was reopened',
    supportCreate:'Create diagnostic report',supportLogs:'Open log folder',supportWorking:'Creating the diagnostic report…',
    supportCopy:'Copy diagnostic info',supportCopying:'Collecting diagnostic info…',supportCopied:'Diagnostic info copied. Paste it into a comment or chat.',
    supportChecks:'Startup self-check',supportChecking:'Checking…',supportCheckOk:'OK',supportCheckProblem:'Problem',supportCheckUnknown:'Unknown',supportFix:'Suggestion: ',
    supportDone:'Saved to your desktop: {file}. Please send this file to us.',supportFailed:'Could not create the report: {error}',supportUnavailable:'The diagnostic report is not available right now.',
    language:'Language',languageDescription:'Choose the display language for Lume.',balancedMode:'Balanced mode',
    balancedDescription:'Play animated wallpapers at 30 fps and pause them in fullscreen.',autostartLabel:'Start Lume with Windows',
    dataFolder:'User data folder',dataFolderDescription:'Account, to-dos, music library and settings are stored here. A change is moved and applied at the next launch.',
    wallpaperFolder:'Wallpaper folder',wallpaperFolderDescription:'Wallpaper files are stored here. A change applies from the next launch and only to wallpapers downloaded afterwards.',
    changeFolder:'Change…',
    autostartDescription:'Start Lume automatically when you sign in to Windows.',version:'Version',checkUpdate:'Check for updates',
    downloadUpdate:'Download and install',installUpdate:'Install now',loading:'Loading settings...',saving:'Saving...',
    checking:'Checking for updates...',current:'You are up to date',available:'A compatible update is available',
    incompatible:'This update is incompatible and cannot be installed directly.',downloading:'Downloading update',
    ready:'Update downloaded and ready to install',installing:'Starting the installer...',error:'The update failed. Please try again.',
    save:'Could not save. Your previous settings have been restored.',network:'Network connection failed. Please try again.',
    startupError:'Could not save the startup setting. Please try again.',unavailable:'The host is disconnected. Settings were not saved.',
    unsupported:'This operation is currently unavailable.',failure:'The operation failed. Please try again.',
    readError:'Could not read the settings. Please try again.',balancedError:'Could not apply the wallpaper playback policy.',
    balancedRollback:'Could not restore the wallpaper playback policy.',startupRollback:'Could not restore the startup setting.',
    startupConflict:'A startup entry belongs to another installation or is unrecognized. It has not been changed.',
    updateDisabled:'Downloading and installing updates is not supported in this version.',
    updateInvalid:'Update verification failed. The update cannot be installed.',updateTimeout:'The update check timed out. Please try again.',
    updateCancelled:'Update check cancelled.',
    diagnosticDescription:'Lume records module health and error states automatically. Add a note only if you want, then copy or export the redacted summary.',
    diagnosticPrivacy:'Runtime summaries are stored locally. You choose whether to share an export; nothing is uploaded automatically. Window and runtime state cannot prove every pixel is rendered correctly.',
    diagnosticSummary:'Additional description (optional)',diagnosticSummaryPlaceholder:'Add what you saw if you want to provide more context',
    diagnosticCopy:'Copy diagnostic summary',diagnosticExport:'Export diagnostic package',
    diagnosticClear:'Clear local records',diagnosticEvents:'{count} runtime events recorded',diagnosticNative:'Native diagnostics: {status}',
    diagnosticMismatch:'{count} effect mismatches detected',diagnosticFault:'{count} native faults detected',
    diagnosticNativeReady:'Recording normally',diagnosticNativeMemory:'Current process only',diagnosticNativeUnavailable:'Unavailable',diagnosticNativeFailed:'Local recording failed',
    diagnosticMismatchDetail:'{component}: expected {expected}, observed {actual}{reason}',
    diagnosticFaultDetail:'{component}: {code}{message}{count}',diagnosticReason:' ({reason})',diagnosticFaultMessage:' ({message})',diagnosticRepeat:'; repeated {count} times',
    diagnosticCopied:'Diagnostic summary copied. You can send it to the developer or AI.',
    diagnosticExported:'Diagnostic package exported.',diagnosticCleared:'Local diagnostic records cleared.',
    diagnosticClearFailed:'Native records could not be cleared. Try again later.',diagnosticClearConfirm:'Clear the issue note and runtime events?',
  },
};
const errorKeys={
  save:'save',storage:'save',language:'save','balanced-mode':'save','preferences-save':'save',
  'preferences-read':'readError','preferences-load':'readError','preferences-invalid':'readError','preferences-version':'readError','not-restored':'readError',
  autostart:'startupError',startup:'startupError','autostart-read':'startupError','autostart-write':'startupError',
  'autostart-command':'startupError','autostart-rollback':'startupRollback','balanced-runtime':'balancedError','balanced-rollback':'balancedRollback',
  'autostart-conflict':'startupConflict',
  'startup-read':'startupError','startup-write':'startupError','startup-rollback':'startupRollback',
  network:'network','update-network':'network','update-offline':'network','update-http':'network',
  unavailable:'unavailable',unsupported:'unsupported','update-disabled':'updateDisabled','update-unavailable':'updateDisabled','update-incompatible':'incompatible',
  'update-manifest':'updateInvalid','update-version':'updateInvalid','update-verification':'updateInvalid','update-tls':'updateInvalid',
  'update-response':'updateInvalid','update-too-large':'updateInvalid','update-redirect':'updateInvalid',
  'update-timeout':'updateTimeout','update-cancelled':'updateCancelled',
  'update-checksum':'updateInvalid','update-cache':'failure','update-installer':'failure','update-install-cancelled':'updateCancelled',
};
// 与原 SettingsWindow 相同的 Font Awesome 图标路径。
const icon=(name,path,width=512)=>`<svg data-icon="${name}" viewBox="0 0 ${width} 512" aria-hidden="true"><path fill="currentColor" d="${path}"/></svg>`;
const sectionIcon={
  general:icon('sliders','M32 64C14.3 64 0 78.3 0 96s14.3 32 32 32l86.7 0c12.3 28.3 40.5 48 73.3 48s61-19.7 73.3-48L480 128c17.7 0 32-14.3 32-32s-14.3-32-32-32L265.3 64C253 35.7 224.8 16 192 16s-61 19.7-73.3 48L32 64zm0 160c-17.7 0-32 14.3-32 32s14.3 32 32 32l246.7 0c12.3 28.3 40.5 48 73.3 48s61-19.7 73.3-48l54.7 0c17.7 0 32-14.3 32-32s-14.3-32-32-32l-54.7 0c-12.3-28.3-40.5-48-73.3-48s-61 19.7-73.3 48L32 224zm0 160c-17.7 0-32 14.3-32 32s14.3 32 32 32l54.7 0c12.3 28.3 40.5 48 73.3 48s61-19.7 73.3-48L480 448c17.7 0 32-14.3 32-32s-14.3-32-32-32l-246.7 0c-12.3-28.3-40.5-48-73.3-48s-61 19.7-73.3 48L32 384z'),
  user:icon('user-gear','M256.5 8a120 120 0 1 1 0 240 120 120 0 1 1 0-240zM226.7 304l59.4 0 1.5 0c-12.9 26.8-7.8 58.2 11.5 79.5-20.2 22.3-24.8 55.8-9.4 83.4l22.5 40.4c.9 1.6 1.9 3.2 2.9 4.7l-237 0c-16.4 0-29.7-13.3-29.7-29.7 0-98.5 79.8-178.3 178.3-178.3zm205.9-56.4c0-13.3 10.7-24 24-24l48 0c13.3 0 24 10.7 24 24l0 6.1c0 18.9 24.1 32.8 40.5 23.4l5-2.9c11.6-6.7 26.5-2.6 33 9.1l22.4 40.2c6.2 11.2 2.6 25.2-8.2 32l-4.7 2.9c-16.2 10.1-16.2 39.9 0 50.1l4.6 2.9c10.8 6.8 14.5 20.8 8.3 32L607 483.8c-6.5 11.7-21.4 15.9-33 9.1l-4.9-2.9c-16.4-9.5-40.5 4.5-40.5 23.4l0 6.1c0 13.3-10.7 24-24 24l-48 0c-13.3 0-24-10.7-24-24l0-5.9c0-19-24.2-33-40.7-23.5l-4.8 2.8c-11.6 6.7-26.4 2.6-33-9.1l-22.6-40.4c-6.2-11.2-2.6-25.3 8.3-32.1l4.4-2.7c16.3-10.1 16.3-40.1 0-50.2l-4.5-2.8c-10.9-6.8-14.5-20.9-8.3-32.1l22.5-40.3c6.5-11.7 21.4-15.8 32.9-9.1l4.8 2.8c16.5 9.5 40.7-4.5 40.7-23.5l0-5.9zm99.9 136.2a52 52 0 1 0 -104 0 52 52 0 1 0 104 0z',640),
  autostart:icon('rocket','M128 320L24.5 320c-24.9 0-40.2-27.1-27.4-48.5L50 183.3C58.7 168.8 74.3 160 91.2 160l95 0c76.1-128.9 189.6-135.4 265.5-124.3 12.8 1.9 22.8 11.9 24.6 24.6 11.1 75.9 4.6 189.4-124.3 265.5l0 95c0 16.9-8.8 32.5-23.3 41.2l-88.2 52.9c-21.3 12.8-48.5-2.6-48.5-27.4L192 384c0-35.3-28.7-64-64-64l-.1 0zM400 160a48 48 0 1 0 -96 0 48 48 0 1 0 96 0z'),
  diagnostics:icon('wrench','M497.9 115.7c-13.1-13.1-34.4-13.1-47.5 0l-59.1 59.1-44.1-44.1 59.1-59.1c13.1-13.1 13.1-34.4 0-47.5-57.6-57.6-151-57.6-208.6 0-37.8 37.8-50.2 91.7-37.3 140.3L29.8 334.9c-39.7 39.7-39.7 104 0 143.7s104 39.7 143.7 0l170.5-170.5c48.6 12.9 102.5.5 140.3-37.3 57.6-57.6 57.6-151 0-208.6zM125.6 435.8c-16 0-29-13-29-29s13-29 29-29 29 13 29 29-13 29-29 29z'),
  support:icon('message-exclamation','M64 0C28.7 0 0 28.7 0 64L0 352c0 35.3 28.7 64 64 64l96 0 0 80c0 6.1 3.4 11.6 8.8 14.3s11.9 2.1 16.8-1.5L309.3 416 448 416c35.3 0 64-28.7 64-64l0-288c0-35.3-28.7-64-64-64L64 0zM256 96c13.3 0 24 10.7 24 24l0 112c0 13.3-10.7 24-24 24s-24-10.7-24-24l0-112c0-13.3 10.7-24 24-24zM224 320a32 32 0 1 1 64 0 32 32 0 1 1 -64 0z'),
  about:icon('circle-info','M256 512a256 256 0 1 0 0-512 256 256 0 1 0 0 512zM224 160a32 32 0 1 1 64 0 32 32 0 1 1 -64 0zm-8 64l48 0c13.3 0 24 10.7 24 24l0 88 8 0c13.3 0 24 10.7 24 24s-10.7 24-24 24l-80 0c-13.3 0-24-10.7-24-24s10.7-24 24-24l24 0 0-64-24 0c-13.3 0-24-10.7-24-24s10.7-24 24-24z'),
};

/**
 * 主线程接口：mountApplicationSettings(host,{bridge,onBack,onLanguage})。
 * render(section?) 复用/重新挂接现有 DOM；show(section='general') 显示并首次请求两个 snapshot；
 * hide() 保留分栏滚动及草稿；handleMessage(message) 对有效设置/账号快照返回 true（陈旧消息仅消费）；
 * requestSnapshot() 手动重试；dispose() 释放子控制器与事件，之后调用不再操作 DOM。
 * logout() 委托同一个账号子控制器，供标题栏复用请求与忙碌保护，不改变页面。
 * onAccountBusyChange(boolean) 透传账号子控制器的忙碌变化，由组合层同步其他账号入口。
 * onBack() 只负责回到原页面；onLanguage(language) 随有效显示语言变化调用，包括失败回退；
 * onUpdateChange(update,busy) 同步更新状态及请求锁，由组合层管理内部弹窗。
 * 主线程应加载 application-settings.css，并将 onLanguage 连接 titlebar.setLanguage。
 * DOM：button[data-settings-section]、[data-settings-panel] 使用 general/user/autostart/diagnostics/about。
 */
export function mountApplicationSettings(host,{bridge,onBack=()=>{},onLanguage=()=>{},onAccountBusyChange=()=>{},onUpdateChange=()=>{},
  diagnostics=null,getDiagnosticsContext=()=>({})}={}) {
  const doc=host.ownerDocument,root=doc.createElement('div');
  root.className='application-settings';root.dataset.applicationSettings='';
  root.innerHTML=`<nav class="application-settings__nav"><div class="application-settings__title-row">
    <button type="button" class="NavigationBackButton" data-settings-back>${arrow}</button><h1 data-settings-text="settings"></h1></div>
    ${sections.map(section=>`<button type="button" data-settings-section="${section}">${sectionIcon[section]}<span data-settings-text="${section}"></span></button>`).join('')}</nav>
    <section class="application-settings__content"><h2 data-settings-heading></h2><div class="application-settings__divider"></div>
    <div class="application-settings__body"><div class="application-settings__panel" data-settings-panel="general">
    <div class="application-settings__fields"><label class="application-settings__field"><span class="application-settings__label" data-settings-text="language"></span>
    <span class="application-settings__description" data-settings-text="languageDescription"></span>
    <select data-setting="language"><option value="zh-CN">简体中文</option><option value="en">English</option></select></label>
    <div class="application-settings__field"><div class="application-settings__label" data-settings-text="balancedMode"></div>
    <p class="application-settings__description" data-settings-text="balancedDescription"></p><label class="application-settings__switch-row">
    <input type="checkbox" role="switch" data-setting="balancedMode"><span data-settings-text="balancedMode"></span></label></div>
    <div class="application-settings__field"><div class="application-settings__label" data-settings-text="dataFolder"></div>
    <p class="application-settings__description" data-settings-text="dataFolderDescription"></p>
    <div class="application-settings__path-row"><code class="application-settings__path" data-settings-path="data">—</code>
    <button type="button" class="application-settings__path-button" data-settings-action="chooseDataDirectory" data-settings-text="changeFolder"></button></div></div>
    <div class="application-settings__field"><div class="application-settings__label" data-settings-text="wallpaperFolder"></div>
    <p class="application-settings__description" data-settings-text="wallpaperFolderDescription"></p>
    <div class="application-settings__path-row"><code class="application-settings__path" data-settings-path="wallpaper">—</code>
    <button type="button" class="application-settings__path-button" data-settings-action="chooseWallpaperDirectory" data-settings-text="changeFolder"></button></div></div></div></div>
    <div class="application-settings__panel" data-settings-panel="user" hidden><div data-account-host></div></div>
    <div class="application-settings__panel" data-settings-panel="autostart" hidden><div class="application-settings__field">
    <div class="application-settings__label" data-settings-text="autostartLabel"></div>
    <p class="application-settings__description" data-settings-text="autostartDescription"></p><label class="application-settings__switch-row">
    <input type="checkbox" role="switch" data-setting="autostart"><span data-settings-text="autostartLabel"></span></label></div></div>
    <div class="application-settings__panel application-settings__diagnostics" data-settings-panel="support" hidden>
    <div class="application-settings__diagnostics-intro"><div class="application-settings__label" data-settings-text="support"></div>
    <p class="application-settings__description" data-settings-text="supportDescription"></p>
    <p class="application-settings__privacy" data-settings-text="supportPrivacy"></p></div>
    <div class="application-settings__diagnostics-fields">
    <label class="application-settings__field"><span class="application-settings__label" data-settings-text="supportNote"></span>
    <textarea data-support-note data-settings-placeholder="supportNotePlaceholder" rows="4" maxlength="1000"></textarea></label></div>
    <div class="application-settings__diagnostics-actions">
    <button type="button" data-support-action="create"><span class="application-settings__glyph" aria-hidden="true">&#xE896;</span><span data-settings-text="supportCreate"></span></button>
    <button type="button" data-support-action="copy"><span class="application-settings__glyph" aria-hidden="true">&#xE8C8;</span><span data-settings-text="supportCopy"></span></button>
    <button type="button" data-support-action="open-logs"><span class="application-settings__glyph" aria-hidden="true">&#xE838;</span><span data-settings-text="supportLogs"></span></button></div>
    <p class="application-settings__diagnostics-events" data-support-status role="status"></p>
    <div class="application-settings__field"><div class="application-settings__label" data-settings-text="supportChecks"></div>
    <div class="application-settings__diagnostics-evidence" data-support-checks></div></div></div>
    <div class="application-settings__panel application-settings__diagnostics" data-settings-panel="diagnostics" hidden>
    <div class="application-settings__diagnostics-intro"><div class="application-settings__label" data-settings-text="diagnostics"></div>
    <p class="application-settings__description" data-settings-text="diagnosticDescription"></p>
    <p class="application-settings__privacy" data-settings-text="diagnosticPrivacy"></p></div>
    <div class="application-settings__diagnostics-fields">
    <label class="application-settings__field"><span class="application-settings__label" data-settings-text="diagnosticSummary"></span>
    <textarea data-diagnostic-field="summary" data-settings-placeholder="diagnosticSummaryPlaceholder" rows="4" maxlength="240"></textarea></label></div>
    <div class="application-settings__diagnostics-actions">
    <button type="button" data-diagnostic-action="copy"><span class="application-settings__glyph" aria-hidden="true">&#xE8C8;</span><span data-settings-text="diagnosticCopy"></span></button>
    <button type="button" data-diagnostic-action="export"><span class="application-settings__glyph" aria-hidden="true">&#xE896;</span><span data-settings-text="diagnosticExport"></span></button>
    <button type="button" data-diagnostic-action="clear"><span class="application-settings__glyph" aria-hidden="true">&#xE74D;</span><span data-settings-text="diagnosticClear"></span></button></div>
    <p class="application-settings__diagnostics-events" data-diagnostic-events></p>
    <p class="application-settings__diagnostics-events" data-diagnostic-native></p>
    <p class="application-settings__diagnostics-events" data-diagnostic-mismatches></p>
    <p class="application-settings__diagnostics-events" data-diagnostic-faults></p>
    <div class="application-settings__diagnostics-evidence" data-diagnostic-evidence></div>
    </div>
    <div class="application-settings__panel application-settings__about" data-settings-panel="about" hidden>
    <div class="application-settings__brand"><div class="application-settings__logo">${LUME_LOGO}</div><div class="application-settings__brand-name">LumeDesktop</div>
    <div class="application-settings__version"><span data-settings-text="version"></span> 1.0.8</div>
    <div class="application-settings__update-actions">${['checkUpdate','downloadUpdate','installUpdate'].map(action=>`<button type="button" data-settings-action="${action}">${action==='checkUpdate'?REFRESH_ICON:`<span class="application-settings__glyph" aria-hidden="true">${action==='downloadUpdate'?'&#xE896;':'&#xE74E;'}</span>`}<span data-settings-text="${action}"></span></button>`).join('')}</div>
    <div data-update-status role="status" aria-live="polite"></div><progress data-update-progress max="100" hidden></progress></div>
    <div class="application-settings__copyright">Copyright 2026 LumeDesktop</div></div></div>
    <p class="application-settings__message" data-settings-message role="status" aria-live="polite"></p></section>`;
  host.replaceChildren(root);
  const $=selector=>root.querySelector(selector),panels=[...root.querySelectorAll('[data-settings-panel]')];
  const account=mountAccount($('[data-account-host]'),{bridge,onBusyChange:onAccountBusyChange});
  let state={revision:-1,language:'zh-CN',balancedMode:true,autostart:true,busy:false,error:'',update:{status:'idle',version:'',progress:0}};
  let confirmed={language:'zh-CN',balancedMode:true,autostart:true};
  let section='general',pending=null,localError='',localNotice='',disposed=false,requested=false,displayLanguage='zh-CN',pendingDiagnosticClear=null;
  const scroll=new Map();
  // 脱离文档或 display:none 后浏览器会把 scrollTop 读成 0，不能用它覆盖离开前的位置。
  const captureScroll=()=>{
    if(!root.isConnected || root.hidden)return;
    panels.forEach(panel=>{if(!panel.hidden)scroll.set(panel.dataset.settingsPanel,panel.scrollTop);});
  };
  const effective=key=>pending?.action===key?pending.fields.value:confirmed[key];
  function sync() {
    if(disposed) return;
    const language=effective('language'),t=text[language];
    root.lang=language;root.dataset.settingsSection=section;
    root.querySelector('nav').setAttribute('aria-label',t.categories);
    root.querySelectorAll('[data-settings-text]').forEach(e=>{e.textContent=t[e.dataset.settingsText];});
    root.querySelectorAll('[data-settings-placeholder]').forEach(e=>{e.placeholder=t[e.dataset.settingsPlaceholder];});
    $('[data-settings-heading]').textContent=t[section];
    $('[data-settings-back]').title=t.back;$('[data-settings-back]').setAttribute('aria-label',t.back);
    root.querySelectorAll('button[data-settings-section]').forEach(button=>{
      const selected=button.dataset.settingsSection===section;
      button.classList.toggle('active',selected);
      if(selected)button.setAttribute('aria-current','page');else button.removeAttribute('aria-current');
    });
    const busy=state.busy || !!pending || state.revision<0;
    // 两个目录只读显示：数据目录固定，壁纸目录由宿主解析后下发。
    $('[data-settings-path="data"]').textContent=state.dataDirectory || '—';
    $('[data-settings-path="wallpaper"]').textContent=state.wallpaperDirectory || '—';
    syncSettingsControlBusy($('[data-settings-action="chooseWallpaperDirectory"]'),busy);
    for(const key of ['language','balancedMode','autostart']) {
      const control=$(`[data-setting="${key}"]`);
      if(key==='language')control.value=effective(key);
      else {control.checked=effective(key);control.setAttribute('aria-checked',String(effective(key)));}
      syncSettingsControlBusy(control,busy);
    }
    const u=state.update,updateBusy=busy || ['checking','downloading','installing'].includes(u.status);
    for(const action of ['checkUpdate','downloadUpdate','installUpdate']) {
      const button=$(`[data-settings-action="${action}"]`);
      button.hidden=action==='downloadUpdate'?u.status!=='available':action==='installUpdate'?u.status!=='ready':false;
      syncSettingsControlBusy(button,updateBusy);
    }
    const status=$('[data-update-status]');
    status.textContent=u.status==='idle'?'':`${t[u.status] || ''}${['available','ready'].includes(u.status)&&u.version?` ${u.version}`:''}${u.status==='downloading'?` ${u.progress}%`:''}`;
    status.setAttribute('role',u.status==='error'?'alert':'status');
    $('[data-update-progress]').hidden=u.status!=='downloading';
    $('[data-update-progress]').value=u.progress;
    $('[data-update-progress]').setAttribute('aria-label',t.downloading);
    const error=localError || state.error,message=$('[data-settings-message]');
    const code=error.toLowerCase().replaceAll('_','-');
    const errorKey=errorKeys[code] || 'failure';
    message.textContent=localNotice || (error?t[errorKey]:state.revision<0?t.loading:pending?(pending.action==='checkUpdate'?t.checking:t.saving):'');
    message.hidden=!message.textContent;message.classList.toggle('error',!!error && !localNotice);
    message.setAttribute('role',error?'alert':'status');
    account.setLanguage(language);
    if(displayLanguage!==language) {displayLanguage=language;onLanguage(language);}
    onUpdateChange({...u},updateBusy);
    if (diagnostics) {
      const note=diagnostics.note();
      for (const key of ['summary']) {
        const field=$(`[data-diagnostic-field="${key}"]`);
        if (field && doc.activeElement !== field && field.value !== note[key]) field.value=note[key];
      }
      const events=$('[data-diagnostic-events]');
      if (events) events.textContent=t.diagnosticEvents.replace('{count}',String(diagnostics.diagnosticCount?.() ?? diagnostics.list().length));
      const native=diagnostics.native?.() || {};
      const nativeStatus=native.storage==='failed' ? t.diagnosticNativeFailed :
        native.storage==='unavailable' ? (native.available===false ? t.diagnosticNativeUnavailable : t.diagnosticNativeMemory) :
        (native.available===false ? t.diagnosticNativeUnavailable : t.diagnosticNativeReady);
      const nativeLabel=$('[data-diagnostic-native]');
      if (nativeLabel) nativeLabel.textContent=t.diagnosticNative.replace('{status}',nativeStatus);
      const mismatches=diagnostics.mismatches?.() || native.mismatches || [];
      const mismatchLabel=$('[data-diagnostic-mismatches]');
      if (mismatchLabel) mismatchLabel.textContent=mismatches.length
        ? t.diagnosticMismatch.replace('{count}',String(mismatches.length)) : '';
      const faultsLabel=$('[data-diagnostic-faults]');
      if (faultsLabel) faultsLabel.textContent=(native.faults || []).length
        ? t.diagnosticFault.replace('{count}',String(native.faults.length)) : '';
      const evidence=$('[data-diagnostic-evidence]');
      if(evidence) {
        evidence.replaceChildren();
        const componentName=value=>({dock:'Dock',taskbar:language==='en'?'Taskbar':'任务栏',wallpaper:language==='en'?'Wallpaper':'壁纸',
          icons:language==='en'?'Desktop icons':'桌面图标',organizer:language==='en'?'Organizer':'图标收纳',weather:language==='en'?'Weather':'天气',
          calendar:language==='en'?'Calendar':'日历',performance:language==='en'?'Performance':'性能',almanac:language==='en'?'Almanac':'黄历'})[value] || value;
        for(const mismatch of mismatches.slice(0,12)) {
          const row=doc.createElement('p');
          row.className='application-settings__diagnostics-evidence-item';
          const reason=mismatch.reason?t.diagnosticReason.replace('{reason}',mismatch.reason):'';
          row.textContent=t.diagnosticMismatchDetail.replace('{component}',componentName(mismatch.component))
            .replace('{expected}',mismatch.expected).replace('{actual}',mismatch.actual).replace('{reason}',reason);
          evidence.append(row);
        }
        for(const fault of (native.faults||[]).slice(0,12)) {
          const row=doc.createElement('p');
          row.className='application-settings__diagnostics-evidence-item';
          const message=fault.message?t.diagnosticFaultMessage.replace('{message}',fault.message):'';
          const count=fault.count>1?t.diagnosticRepeat.replace('{count}',String(fault.count)):'';
          row.textContent=t.diagnosticFaultDetail.replace('{component}',componentName(fault.component))
            .replace('{code}',fault.code).replace('{message}',message).replace('{count}',count);
          evidence.append(row);
        }
      }
    }
  }
  let supportBusy=false,supportStatus='',supportChecks=null;
  function syncSupport() {
    const t=text[effective('language')];
    const status=$('[data-support-status]');
    if(status){status.textContent=supportStatus;status.hidden=!supportStatus;}
    for(const action of ['create','copy']) {
      const button=$(`[data-support-action="${action}"]`);
      if(button){button.disabled=supportBusy;button.setAttribute('aria-busy',String(supportBusy));}
    }
    const list=$('[data-support-checks]');
    if(!list)return;
    list.replaceChildren();
    if(!supportChecks){const row=doc.createElement('p');row.className='application-settings__diagnostics-evidence-item';row.textContent=t.supportChecking;list.append(row);return;}
    for(const check of supportChecks) {
      const row=doc.createElement('p');
      row.className='application-settings__diagnostics-evidence-item';
      row.dataset.checkStatus=check.status;
      const mark=check.status==='ok'?t.supportCheckOk:check.status==='problem'?t.supportCheckProblem:t.supportCheckUnknown;
      row.textContent=`[${mark}] ${check.title}${check.detail?'：'+check.detail:''}`;
      list.append(row);
      if(check.fix){const fix=doc.createElement('p');fix.className='application-settings__diagnostics-evidence-item';fix.textContent=t.supportFix+check.fix;list.append(fix);}
    }
  }
  function render(nextSection) {
    if(disposed) return null;
    if(nextSection==='diagnostics'&&!DIAGNOSTICS_ENABLED)nextSection='general';
    captureScroll();
    if(sections.includes(nextSection))section=nextSection;
    if(root.parentNode!==host)host.replaceChildren(root);
    panels.forEach(panel=>{panel.hidden=panel.dataset.settingsPanel!==section;});
    const panel=$(`[data-settings-panel="${section}"]`);
    panel.scrollTop=scroll.get(section) || 0;
    sync();
    if(section==='diagnostics')try {bridge?.diagnostics?.('snapshot');} catch {}
    if(section==='support'){supportChecks=null;syncSupport();try {bridge?.diagnosticReport?.('self-check');} catch {}}
    return root;
  }
  function request(action,fields={}) {
    if(disposed) return false;
    const snapshot=action==='snapshot';
    // 选择目录由用户直接取消或确认，不进入“保存中”，否则取消后界面会卡在保存态。
    const picking=action==='chooseWallpaperDirectory';
    if(!snapshot && (state.revision<0 || state.busy || pending)) return false;
    if(action==='downloadUpdate' && state.update.status!=='available')return false;
    if(action==='installUpdate' && state.update.status!=='ready')return false;
    if(!snapshot && !picking) {pending={action,fields,revision:state.revision};localError='';sync();}
    let sent=false;
    try {sent=bridge?.applicationSettings?.(action,fields)===true;} catch { /* 不显示宿主异常内容。 */ }
    if(!sent) {if(!snapshot && !picking)pending=null;localError='unavailable';sync();}
    return sent;
  }
  function handleMessage(message) {
    if(disposed) return false;
    if(message?.type==='account-state')return account.handleMessage(message);
    if(message?.type==='diagnostic-report-state') {
      const t=text[effective('language')];
      const action=message.action||'create';
      if(action==='self-check') {
        if(message.status==='done'&&Array.isArray(message.checks))supportChecks=message.checks;
        syncSupport();return true;
      }
      supportBusy=message.status==='working';
      supportStatus=message.status==='working'?(action==='copy'?t.supportCopying:t.supportWorking):
        message.status==='done'?(action==='copy'?t.supportCopied:t.supportDone.replace('{file}',message.file||'')):
        t.supportFailed.replace('{error}',message.error||'');
      syncSupport();
      return true;
    }
    if(message?.type==='diagnostics-state') {
      if(!DIAGNOSTICS_ENABLED)return false;
      diagnostics?.record?.(message);
      sync();
      if(pendingDiagnosticClear!==null&&message.revision>pendingDiagnosticClear) {
        pendingDiagnosticClear=null;
        if(message.storage!=='failed') {
          diagnostics?.clear();diagnostics?.clearNote();diagnostics?.record?.(message);
        }
        setDiagnosticMessage(message.storage==='failed'
          ? text[effective('language')].diagnosticClearFailed
          : text[effective('language')].diagnosticCleared);
      }
      return true;
    }
    if(!validApplicationSettingsState(message))return false;
    if(message.revision<=state.revision)return true;
    state={...message,update:{...message.update}};
    // settled 快照始终携带 native 已确认值；回滚失败也必须显示实际值，不能捏造恢复成功。
    if(!message.busy)confirmed={language:message.language,balancedMode:message.balancedMode,autostart:message.autostart};
    if(pending && !message.busy && message.revision>pending.revision)pending=null;
    localError='';sync();
    return true;
  }
  const onClick=event=>{
    const button=event.target.closest?.('button');
    if(!button || !root.contains(button) || button.closest('[hidden]'))return;
    if(button.hasAttribute('data-settings-back')) {hide();onBack();return;}
    if(button.dataset.settingsSection) {render(button.dataset.settingsSection);return;}
    if(button.dataset.settingsAction && !button.disabled && button.getAttribute('aria-disabled')!=='true')
      request(button.dataset.settingsAction);
    if(button.dataset.supportAction) {
      if((button.dataset.supportAction==='create'||button.dataset.supportAction==='copy')&&supportBusy)return;
      const note=$('[data-support-note]')?.value?.slice(0,1000) || '';
      let sent=false;
      try {sent=bridge?.diagnosticReport?.(button.dataset.supportAction,note)===true;} catch { /* 宿主不可用时提示即可。 */ }
      const t=text[effective('language')];
      if(!sent) supportStatus=t.supportUnavailable;
      else if(button.dataset.supportAction==='create') {supportBusy=true;supportStatus=t.supportWorking;}
      else if(button.dataset.supportAction==='copy') {supportBusy=true;supportStatus=t.supportCopying;}
      syncSupport();
      return;
    }
    if(DIAGNOSTICS_ENABLED&&button.dataset.diagnosticAction) {
      const note=readDiagnosticNote();
      if(button.dataset.diagnosticAction==='clear') {
        const confirmed=typeof globalThis.confirm==='function' ? globalThis.confirm(text[effective('language')].diagnosticClearConfirm) : true;
        if(confirmed) {
          const nativeWasAvailable=diagnostics?.native?.().available===true;
          if(pendingDiagnosticClear!==null)return;
          const revision=diagnostics?.native?.().revision??0;
          pendingDiagnosticClear=revision;
          let sent=false;
          try { sent=bridge?.diagnostics?.('clear')===true; } catch { /* native diagnostics are optional */ }
          if(!sent && nativeWasAvailable) {
            pendingDiagnosticClear=null;
            setDiagnosticMessage(text[effective('language')].diagnosticClearFailed);return;
          }
          if(!sent) {
            pendingDiagnosticClear=null;
            diagnostics?.clear();diagnostics?.clearNote();
            setDiagnosticMessage(text[effective('language')].diagnosticCleared);
          } else setTimeout(()=>{
            if(!disposed&&pendingDiagnosticClear===revision) {
              pendingDiagnosticClear=null;
              setDiagnosticMessage(text[effective('language')].diagnosticClearFailed);
            }
          },5000);
        }
      } else if(button.dataset.diagnosticAction==='copy') copyDiagnostics(note);
      else if(button.dataset.diagnosticAction==='export') exportDiagnostics(note);
    }
  };
  const readDiagnosticNote=()=>Object.fromEntries(['summary'].map(key=>[
    key,$(`[data-diagnostic-field="${key}"]`)?.value || "",
  ]));
  const setDiagnosticMessage=message=>{
    localError='';localNotice=message;sync();
    setTimeout(()=>{if(localNotice===message){localNotice='';sync();}},3500);
  };
  const reportFor=note=>{
    const context=typeof getDiagnosticsContext==='function'?getDiagnosticsContext():{};
    return diagnostics?.buildReport ? diagnostics.buildReport({
      ...context, note, native: diagnostics.native?.(), mismatches: diagnostics.mismatches?.(),
    }) : null;
  };
  const copyDiagnostics=async note=>{
    const report=reportFor(note);if(!report)return;
    const value=diagnostics.text(report);
    try {
      if (typeof globalThis.navigator?.clipboard?.writeText==='function') await globalThis.navigator.clipboard.writeText(value);
      else {
        const area=doc.createElement('textarea');area.value=value;area.style.position='fixed';area.style.opacity='0';
        doc.body.append(area);area.select();doc.execCommand?.('copy');area.remove();
      }
      diagnostics.saveNote(note);setDiagnosticMessage(text[effective('language')].diagnosticCopied);
    } catch {setDiagnosticMessage(text[effective('language')].failure);}
  };
  const exportDiagnostics=note=>{
    const report=reportFor(note);if(!report)return;
    try {
      const blob=new Blob([diagnostics.text(report)],{type:'application/json'});
      const url=URL.createObjectURL(blob),anchor=doc.createElement('a');
      anchor.href=url;anchor.download=`lume-diagnostic-${new Date().toISOString().replaceAll(':','-')}.json`;
      anchor.click();setTimeout(()=>URL.revokeObjectURL(url),0);
      diagnostics.saveNote(note);setDiagnosticMessage(text[effective('language')].diagnosticExported);
    } catch {setDiagnosticMessage(text[effective('language')].failure);}
  };
  const onChange=event=>{
    const key=event.target.dataset.setting;
    if(!['language','balancedMode','autostart'].includes(key))return;
    if(event.target.getAttribute('aria-disabled')==='true') {sync();return;}
    const value=key==='language'?event.target.value:event.target.checked;
    if(key==='language' && !['zh-CN','en'].includes(value)) {sync();return;}
    request(key,{value});
  };
  const onInput=event=>{
    if(DIAGNOSTICS_ENABLED&&event.target.dataset.diagnosticField && diagnostics) diagnostics.saveNote(readDiagnosticNote());
  };
  const onScroll=event=>{if(event.target.hasAttribute?.('data-settings-panel'))scroll.set(event.target.dataset.settingsPanel,event.target.scrollTop);};
  function hide() {if(disposed)return;captureScroll();root.hidden=true;}
  function requestSnapshot() {
    const settingsSent=request('snapshot'),accountSent=account.requestSnapshot();
    let diagnosticsSent=true;
    if(DIAGNOSTICS_ENABLED)try { diagnosticsSent=bridge?.diagnostics?.('snapshot') !== false; } catch { diagnosticsSent=false; }
    return settingsSent && accountSent && diagnosticsSent;
  }
  root.addEventListener('click',onClick);root.addEventListener('change',onChange);root.addEventListener('input',onInput);root.addEventListener('scroll',onScroll,true);
  render();
  return {
    render,handleMessage,hide,requestSnapshot,
    downloadUpdate:()=>request('downloadUpdate'),
    installUpdate:()=>request('installUpdate'),
    logout:()=>account.logout(),
    show(nextSection='general') {
      if(disposed)return null;
      render(nextSection);root.hidden=false;
      $(`[data-settings-panel="${section}"]`).scrollTop=scroll.get(section) || 0;
      if(!requested)requested=requestSnapshot();
      return root;
    },
    dispose() {
      if(disposed)return;
      disposed=true;root.removeEventListener('click',onClick);root.removeEventListener('change',onChange);
      root.removeEventListener('scroll',onScroll,true);root.removeEventListener('input',onInput);account.dispose();pending=null;scroll.clear();root.remove();
    },
  };
}
