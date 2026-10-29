// No separated drum stem exists in Lume: every track reports no drum hits,
// which is the upstream path for a track without drums.
export const DRUM_KICK = 0, DRUM_SNARE = 1, DRUM_HAT = 2;
export const getDrumHits = () => null;