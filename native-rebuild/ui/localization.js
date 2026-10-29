let language = "zh-CN";

// Persistence and rendering belong to the caller; only explicit UI copy is translated.
export function setLanguage(value) {
  language = value === "en" ? "en" : "zh-CN";
  return language;
}

export function getLanguage() {
  return language;
}

export function t(chinese, english = chinese) {
  return language === "en" ? english : chinese;
}
