import { settingsFrame, settingsSelect, syncSettingsSelect } from "./settings-frame.js";
import { t } from "./localization.js";
const styles=()=>[["welfare",t("样式一","Style 1")],["mond",t("样式二","Style 2")],["insideDream",t("样式三","Style 3")]];
function clockStatus(state) {
  if(state.status === "error") return state.error === "runtime" ? t("桌面时钟启动失败，请检查字体和安装文件","Could not start the desktop clock. Check the fonts and installation files.") : state.error === "rollback" ? t("时钟恢复失败，请重新启用组件","Could not restore the clock. Enable the widget again.") : t("保存失败，已恢复此前设置，请重试","Could not save. Previous settings restored. Please try again.");
  return {loading:t("正在读取设置...","Loading settings..."),saving:t("正在保存...","Saving..."),unavailable:t("桌面宿主未连接，设置未保存","Desktop host disconnected. Settings were not saved.")}[state.status] || "";
}
export function clockPage(state,tab="settings") {
  const description=t("在桌面显示时间，提供样式一、样式二、样式三三种样式。","Display the time on your desktop in three styles: Style 1, Style 2 and Style 3.");
  const body=tab === "settings" ? `<div class="setting-row"><label for="clock-style">${t("时间样式","Clock style")}</label>${settingsSelect("clock-style",t("时间样式","Clock style"),state.clock.style,styles(),state.status==="saving")}</div><div class="reset-row"><button class="reset" data-action="reset" ${state.status==="saving"?"disabled":""}>${t("恢复默认","Restore defaults")}</button></div>` : `<div class="settings-details"><h3>${t("功能说明","About this widget")}</h3><p>${description}</p><p>${t("组件可独立启用，桌面位置随拖拽保存。退出 Lume 时，组件会一同关闭。","Enable the widget independently. Its position is saved when dragged. The widget closes when you exit Lume.")}</p></div>`;
  return settingsFrame({title:t("时间","Clock"),description,tab,body,status:clockStatus(state),error:state.status==="error",scope:"clock"});
}
export function updateClockPreview(host, style, now = new Date(), appearance = {}) {
  if (!host) return;
  const day = now.toLocaleDateString("en-US", { weekday: "long" }),
    month = now.toLocaleDateString("en-US", { month: "long" }),
    date = String(now.getDate()).padStart(2, "0");
  const time = `${String(now.getHours()).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}`;
  host.className = "clock-preview " + style;
  host.classList.toggle("custom-color", !!appearance.customColor);
  host.style.setProperty("--clock-color", `#${(appearance.color ?? 0xffffff).toString(16).padStart(6,"0")}`);
  host.style.opacity = String((appearance.alpha ?? 255) / 255);
  const dateText =
    style === "welfare"
      ? `${date} ${month}`
      : style === "mond"
        ? `${date}  ${month},  ${now.getFullYear()}.`
        : `${date} ${month},${now.getFullYear()}.`;
  const timeText =
    style === "mond"
      ? `- ${time} -`
      : style === "insideDream"
        ? `${String(now.getHours() % 12 || 12).padStart(2, "0")}:${String(now.getMinutes()).padStart(2, "0")}|${now.getHours() < 12 ? "AM" : "PM"}`
        : time;
  host.innerHTML = `<div class="clock-day">${day}</div><div class="clock-time">${timeText}</div><div class="clock-date">${dateText}</div>`;
}
export function syncClockControls(root, state) {
  root.querySelectorAll('[data-action="toggle"]').forEach((button) => {
    button.classList.toggle("checked", state.clock.enabled);
    button.setAttribute("aria-checked", String(state.clock.enabled));
    button.disabled = state.status === "saving";
  });
  syncSettingsSelect(root,"clock-style",state.clock.style,styles(),state.status === "saving");
  const reset = root.querySelector('[data-action="reset"]');
  if (reset) reset.disabled = state.status === "saving";
  const status = root.querySelector('[data-settings-scope="clock"] .status');
  if (status) {
    status.textContent = clockStatus(state);
    status.hidden = !status.textContent;
    status.setAttribute('role',state.status === 'error'?'alert':'status');
    status.classList.toggle("error", state.status === "error");
  }
  updateClockPreview(root.querySelector(".clock-preview"), state.clock.style, new Date(), state.clock);
}
