import zhCN from '../vendor/folia/src/i18n/locales/zh-CN';
import en from '../vendor/folia/src/i18n/locales/en';

// Use Folia's own labels with the host language, without its browser i18n runtime.
const valueAt = (object, name) => name.split('.').reduce((value, key) => value?.[key], object);
const i18n = {
    language: 'zh-CN',
    resolvedLanguage: 'zh-CN',
    t(key, options = {}) {
        const hooks = globalThis.__lumeMusicProviderEnvironment;
        if (typeof hooks?.translate === 'function') return hooks.translate(key, options);
        const language = hooks?.language || this.resolvedLanguage;
        const defaults = String(language).toLowerCase().startsWith('en') ? en : zhCN;
        const found = valueAt(hooks?.translations, String(key)) ?? valueAt(defaults, String(key));
        const text = typeof found === 'string' ? found : typeof options === 'string' ? options : options.defaultValue ?? String(key);
        return text.replace(/\{\{\s*(\w+)\s*\}\}/g, (match, name) => options[name] === undefined ? match : String(options[name]));
    },
    async changeLanguage(language) { this.language = this.resolvedLanguage = language || 'zh-CN'; },
    on() { return this; },
    off() { return this; },
};
export default i18n;
