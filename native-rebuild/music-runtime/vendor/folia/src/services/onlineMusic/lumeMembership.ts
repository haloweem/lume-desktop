// Lume adaptation: retain display entitlements across Folia's normalization boundary.
// These markers describe the upstream response; playback authorization remains upstream.
export const lumeSongMembership = (provider: string, raw: any): Record<string, string | boolean> => {
    const item = raw?.songInfo ?? raw?.musicData ?? raw ?? {};
    const kept = item.sourceRef?.providerId === provider ? item.sourceRef.providerData : undefined;
    if (typeof kept?.lumeVipRequired === 'boolean') return {
        lumeVipRequired: kept.lumeVipRequired,
        ...(typeof kept.lumeMembershipBadge === 'string' ? { lumeMembershipBadge: kept.lumeMembershipBadge } : {}),
    };
    const at = (value: any, path: string) => path.split('.').reduce((v, key) => v?.[key], value);
    const values = (paths: string[]) => [item, raw].flatMap(value => paths.map(path => at(value, path)));
    const paid = (paths: string[]) => values(paths).some(value => value === true ||
        ((typeof value === 'number' || typeof value === 'string' && /^\d+$/.test(value)) && Number(value) > 0));
    let required = false, badge = '';
    if (provider === 'qq') {
        required = paid(['pay.pay_play', 'pay.payplay', 'pay_play']);
        if (paid(['isSuperVip', 'is_super_vip', 'superVip', 'super_vip', 'isSvip', 'svip'])) badge = 'qq-super-vip';
        else if (required) badge = 'qq-green-vip';
    } else if (provider === 'netease') {
        const fees = values(['fee', 'privilege.fee']).map(Number);
        required = fees.includes(1) || fees.includes(4) || paid(['privilege.payPlay']);
        if (paid(['redplus', 'redPlus', 'svipLevel', 'privilege.svipLevel'])) badge = 'netease-svip';
        else if (fees.includes(1)) badge = 'netease-vip';
    } else if (provider === 'kugou') {
        const privilege = values(['Privilege', 'privilege', 'privilege_128', 'audio_info.privilege', 'audio_info.privilege_128'])
            .map(Number).find(value => Number.isFinite(value) && value > 0);
        required = privilege !== undefined ? privilege === 10 : paid(['pay_type', 'PayType', 'audio_info.pay_type', 'is_vip', 'audio_info.is_vip']);
        if (required) badge = 'kugou-vip';
    }
    return { lumeVipRequired: required || Boolean(badge), ...(badge ? { lumeMembershipBadge: badge } : {}) };
};
