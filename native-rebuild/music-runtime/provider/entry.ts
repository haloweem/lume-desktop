import { neteaseProvider } from '../vendor/folia/src/services/onlineMusic/netease/neteaseProvider';
import { qqProvider } from '../vendor/folia/src/services/onlineMusic/qq/qqProvider';
import { kugouProvider } from '../vendor/folia/src/services/onlineMusic/kugou/kugouProvider';
import { qishuiProvider } from '../vendor/folia/src/services/onlineMusic/qishui/qishuiProvider';

// Folia's four adapters, with Lume display metadata retention documented in vendor/SOURCES.md.
export const providers = Object.freeze({
    netease: neteaseProvider,
    qq: qqProvider,
    kugou: kugouProvider,
    qishui: qishuiProvider,
});
