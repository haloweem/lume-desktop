import {LUME_LOGO} from "./account.js";

const copy = {
  "zh-CN": {
    quotaTitle: "今日下载额度已用尽",
    quotaGuestTitle: "免费下载额度已用完",
    quotaGuest: "游客共可免费下载 10 张，注册登录后每天可下载 10 张",
    login: "登录",
    confirm: "知道了",
    updateTitle: "发现新版本",
    updateVersion: version => `LumeDesktop ${version}`,
    updateBody: "新版本已发布",
    updateReady: "更新已下载，安装后即可使用",
    updateDownloading: "正在下载更新",
    updateInstalling: "正在启动安装器，请确认 Windows 权限提示",
    updateBlocked: "此版本暂不支持直接安装更新",
    updateCurrent: "已是最新版本",
    updateFailed: "更新失败",
    updateRetry: "请稍后重试",
    later: "稍后",
    download: "下载并安装",
    install: "立即安装",
    close: "关闭",
  },
  en: {
    quotaTitle: "Today's download quota is used up",
    quotaGuestTitle: "Your free downloads are used up",
    quotaGuest: "Guests get 10 free downloads in total. Sign up or sign in for 10 downloads every day.",
    login: "Sign in",
    confirm: "OK",
    updateTitle: "Update available",
    updateVersion: version => `LumeDesktop ${version}`,
    updateBody: "A new version is ready to download.",
    updateReady: "The update is downloaded and ready to install.",
    updateDownloading: "Downloading update",
    updateInstalling: "Starting the installer. Please respond to the Windows permission prompt.",
    updateBlocked: "Direct installation is not supported in this version.",
    updateCurrent: "You are up to date",
    updateFailed: "Update failed",
    updateRetry: "Please try again later.",
    later: "Later",
    download: "Download and install",
    install: "Install now",
    close: "Close",
  },
};

function text(value) {
  return typeof value === "string" ? value : "";
}

function setText(element, value) {
  element.textContent = text(value);
}

export function createLumeModalController(document, {
  onLogin = () => {},
  onUpdateDownload = () => {},
  onUpdateInstall = () => {},
  language = "zh-CN",
} = {}) {
  if (!document?.body) throw new TypeError("A document with a body is required");
  const t = () => copy[language] || copy["zh-CN"];
  const dialog = document.createElement("dialog");
  dialog.className = "lume-modal";
  dialog.setAttribute("aria-modal", "true");
  dialog.innerHTML = `
    <div class="lume-modal__surface">
      <div class="lume-modal__header">
        <div class="lume-modal__brand">${LUME_LOGO}<span>LumeDesktop</span></div>
        <button type="button" class="lume-modal__close" data-lume-modal-close><span aria-hidden="true">&#xE8BB;</span></button>
      </div>
      <h2 class="lume-modal__title" data-lume-modal-title></h2>
      <p class="lume-modal__version" data-lume-modal-version hidden></p>
      <p class="lume-modal__body" data-lume-modal-body></p>
      <div class="lume-modal__actions">
        <button type="button" class="lume-modal__button" data-lume-modal-secondary></button>
        <button type="button" class="lume-modal__button lume-modal__button--primary" data-lume-modal-primary></button>
      </div>
    </div>`;
  document.body.append(dialog);

  const title = dialog.querySelector("[data-lume-modal-title]");
  const version = dialog.querySelector("[data-lume-modal-version]");
  const body = dialog.querySelector("[data-lume-modal-body]");
  const close = dialog.querySelector("[data-lume-modal-close]");
  const secondary = dialog.querySelector("[data-lume-modal-secondary]");
  const primary = dialog.querySelector("[data-lume-modal-primary]");
  let restoreFocus = null;
  let primaryAction = () => {};
  let disposed = false;
  let current = null;
  let lastUpdatePrompt = "";
  let pendingResult = null;
  let pendingUpdate = null;
  let generation = 0;
  let hiddenTransfer = '';
  title.id = "lume-modal-title";
  body.id = "lume-modal-body";
  dialog.setAttribute("aria-labelledby", title.id);

  function closeDialog() {
    if (disposed) return;
    if(current?.kind==='update'&&['downloading','installing'].includes(current.update.status))hiddenTransfer=current.update.version;
    const closedGeneration = ++generation;
    current = null;
    if (dialog.open) dialog.close();
    restore();
    if (pendingResult || pendingUpdate) queueMicrotask(() => {
      if (disposed || dialog.open || generation !== closedGeneration) return;
      if (pendingResult) {
        const result = pendingResult;
        pendingResult = null;
        openDialog({kind:"result",result});
      } else if (pendingUpdate && !pendingUpdate.busy) {
        const {update,busy} = pendingUpdate;
        syncUpdate(update,busy);
      }
    });
  }

  function restore() {
    const target = restoreFocus;
    restoreFocus = null;
    if (target?.isConnected && typeof target.focus === "function") target.focus();
  }

  function render() {
    const labels = t();
    const quota = current.kind === "quota";
    const result = current.kind === "result" ? current.result : null;
    const install = current.update?.status === "ready";
    const downloading = current.update?.status === 'downloading',installing=current.update?.status==='installing';
    const bodyText = quota ? (current.signedIn ? "" : labels.quotaGuest)
      : result ? (result.kind === "current" ? "" : result.kind === "error" ? labels.updateRetry : labels.updateBlocked)
      : downloading ? `${labels.updateDownloading} ${current.update.progress}%` : installing ? labels.updateInstalling
      : install ? labels.updateReady : labels.updateBody;
    setText(title, quota ? (current.signedIn ? labels.quotaTitle : labels.quotaGuestTitle) : result?.kind === "current" ? labels.updateCurrent
      : result?.kind === "error" ? labels.updateFailed : labels.updateTitle);
    const versionText = quota ? "" : result ? (["incompatible","disabled"].includes(result.kind) ? result.updateVersion : "")
      : current.update.version;
    setText(version, versionText ? labels.updateVersion(text(versionText)) : "");
    setText(body, bodyText);
    setText(primary, quota ? (current.signedIn ? labels.confirm : labels.login)
      : result ? labels.confirm : install ? labels.install : labels.download);
    setText(secondary, quota || result ? "" : labels.later);
    version.hidden = !versionText;
    body.hidden = !bodyText;
    secondary.hidden = quota || !!result;
    primary.disabled = !quota && !!current.busy;
    close.setAttribute("aria-label", labels.close);
    close.title = labels.close;
    dialog.lang = copy[language] ? language : "zh-CN";
    dialog.dataset.kind = current.kind;
    if (bodyText) dialog.setAttribute("aria-describedby", body.id);
    else dialog.removeAttribute("aria-describedby");
    primaryAction = quota ? (current.signedIn ? () => {} : onLogin)
      : result ? () => {} : install ? onUpdateInstall : onUpdateDownload;
  }

  function openDialog(next) {
    if (disposed) return false;
    // Network completions and queued results must not interrupt another app or
    // reappear on focus. The settings page retains the update status.
    if (next.kind !== 'quota' && (document.hidden || !document.hasFocus())) return false;
    const opening = !dialog.open;
    const previous = current;
    ++generation;
    if (opening) restoreFocus = document.activeElement;
    current = next;
    render();
    if (opening) dialog.showModal();
    if (opening || previous?.kind !== current.kind ||
        previous?.update?.status !== current.update?.status ||
        previous?.signedIn !== current.signedIn) {
      // Installing restarts the application; do not make Enter accept it by default.
      (current.kind === "update" ? secondary : primary).focus();
    }
    return true;
  }

  close.addEventListener("click", closeDialog);
  dialog.addEventListener("cancel", event => {
    event.preventDefault();
    closeDialog();
  });
  dialog.addEventListener("close", () => {
    // A queued close from the previous dialog must not steal focus after reopening.
    if (!dialog.open) {current = null;restore();}
  });
  primary.addEventListener("click", () => {
    if (disposed || !dialog.open || primary.disabled) return;
    const action = primaryAction;
    if (current?.kind === "update") {
      const updateVersion = current.update.version;
      if (action() === false) return;
      hiddenTransfer='';
      openDialog({kind:'update',update:{status:action===onUpdateInstall?'installing':'downloading',version:updateVersion,progress:0},busy:true});
      return;
    }
    closeDialog();
    action();
  });
  secondary.addEventListener("click", closeDialog);

  function syncUpdate(update, busy = false, allowAvailabilityPrompt = true) {
    if (disposed) return;
    // Ordinary snapshots may refresh an explicit dialog, but cannot open one
    // for availability, restored downloads, progress or installer completion.
    if (!allowAvailabilityPrompt && current?.kind !== 'update') return;
    if(['downloading','installing'].includes(update.status)) {
      if(hiddenTransfer===update.version)return;
      if(current?.kind==='quota'){pendingUpdate={update:{...update},busy,interrupted:true};return;}
      openDialog({kind:'update',update:{...update},busy:true});return;
    }
    if(update.status==='error'&&current?.kind==='update') {
      openDialog({kind:'result',result:{kind:'error',updateVersion:'',error:'update-failed',manual:true}});return;
    }
    if(!['downloading','installing'].includes(update.status))hiddenTransfer='';
    if (!["available","ready"].includes(update.status) || !text(update.version)) {
      pendingUpdate = null;
      if (current?.kind === "update") closeDialog();
      if (["idle","current","error"].includes(update.status)) lastUpdatePrompt = "";
      return;
    }
    const key = `${update.status}:${update.version}`;
    if(update.status==='available'&&!allowAvailabilityPrompt&&current?.kind!=='update')return;
    const interrupted = !!pendingUpdate?.interrupted &&
      `${pendingUpdate.update.status}:${pendingUpdate.update.version}` === key;
    if (current && current.kind !== "update") {
      pendingUpdate = {update:{...update},busy,interrupted};
      return;
    }
    if (current?.kind === "update") {
      pendingUpdate = null;
      lastUpdatePrompt = key;
      openDialog({kind:"update",update:{...update},busy});
    } else if (busy) {
      pendingUpdate = {update:{...update},busy,interrupted};
    } else if (interrupted || key !== lastUpdatePrompt) {
      pendingUpdate = null;
      lastUpdatePrompt = key;
      openDialog({kind:"update",update:{...update},busy});
    } else pendingUpdate = null;
  }

  return {
    showQuota(signedIn = false) {
      if (current?.kind === "update")
        pendingUpdate = {update:{...current.update},busy:current.busy,interrupted:true};
      return openDialog({kind:"quota",signedIn});
    },
    showUpdate(update = {}) {
      if (!["available","ready"].includes(update.status) || !text(update.version)) return false;
      return openDialog({kind:"update",update:{...update},busy:false});
    },
    showUpdateResult(result) {
      if (disposed || result.manual !== true || document.hidden || !document.hasFocus()) return false;
      if(result.kind==='available') {
        lastUpdatePrompt='';syncUpdate({status:'available',version:result.updateVersion,progress:0});return true;
      }
      pendingUpdate = null;
      pendingResult = null;
      if (dialog.open && current?.kind !== "result") {
        pendingResult = {...result};
        return true;
      }
      return openDialog({kind:"result",result:{...result}});
    },
    syncUpdate,
    syncAccount(signedIn) {
      if (current?.kind === "quota") openDialog({kind:"quota",signedIn});
    },
    setLanguage(nextLanguage) {
      if (disposed || !copy[nextLanguage]) return;
      language = nextLanguage;
      if (current) render();
    },
    close: closeDialog,
    element: dialog,
    dispose() {
      if (disposed) return;
      pendingResult = null;
      pendingUpdate = null;
      closeDialog();
      disposed = true;
      dialog.remove();
    },
  };
}
