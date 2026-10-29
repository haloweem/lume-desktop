import { t } from "./localization.js";

export const escapeHTML = (value) =>
  String(value).replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ],
  );
export const arrow =
  '<svg width="18" height="18" viewBox="0 0 24 24" aria-hidden="true"><path d="M19 12H5m7-7-7 7 7 7" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
export const back = (label = t("返回", "Back")) =>
  `<button class="NavigationBackButton" data-action="back" aria-label="${label}" title="${label}">${arrow}</button>`;
export const toggle = (enabled, label, action, disabled = false) =>
  `<button role="switch" aria-checked="${enabled}" aria-label="${escapeHTML(label)}" class="switch${enabled ? " checked" : ""}" data-action="${action}" ${disabled ? "disabled" : ""}><span></span></button>`;
